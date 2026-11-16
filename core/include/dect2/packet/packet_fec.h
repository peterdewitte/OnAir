// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kristijonas Adomaitis and OnAir contributors

// Packet radio with forward error correction, alongside plain AX.25 on the same demodulator: FX.25 (TAPR, Stensby) and IL2P (N. Carrillo KK4HEJ, draft 0.6). Tags, parity sizes and the
// FX.25 block layout were checked against Dire Wolf's source (GPL-2+), IL2P against the v0.6 specification; the scrambler initial values come from Dire Wolf.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace dect2 {

// What the transmitter (the test signal) wraps a frame in
enum class PacketFec : int { None = 0, Fx25 = 1 };

struct PacketFecFrame {
    std::vector<uint8_t> ax25;   // the AX.25 frame without FCS
    int repaired = 0;            // bytes the Reed-Solomon decoders corrected
    bool il2p = false;           // false: FX.25
    int lateBits = 0;            // line bits between the end of the frame and the moment it was handed over (FX.25: padding and parity after the closing flag)
};

class PacketFecRx {
public:
    using Sink = std::function<void(const PacketFecFrame&)>;
    PacketFecRx();
    ~PacketFecRx();
    void setSink(Sink s);
    void reset();
    // one line bit (before NRZI decoding): 1200 baud AFSK gives levels directly, IL2P is not NRZI
    void lineBit(int level, bool il2pActive);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

namespace packetfec {
// the codeblocks, for the test signal
bool fx25Tag(int ctag, uint64_t& value, int& nBytes, int& kBytes);       // false: undefined tag
// The tag for dataBytes of stuffed frame: checkBytes 16, 32 or 64 picks the shortest block with that many check bytes; any other value the choice
// by length that Dire Wolf makes (32 data + 16, 64 + 16, 128 + 32, 191 + 64, 223 + 32, 239 + 16). 0: does not fit in one block.
int fx25PickTag(int checkBytes, int dataBytes);
int rsEncodeParity(const uint8_t* data, int n, int nroots, int fcr, uint8_t* parityOut);   // the Karn field 0x11D, prim 1
std::vector<uint8_t> il2pDescramble(const uint8_t* in, size_t n);
std::vector<uint8_t> il2pScramble(const uint8_t* in, size_t n);
// FX.25 transmit (the test signal): frame bytes with FCS -> the bits ready for NRZI: flagsBefore flags, the correlation tag, the block (the stuffed
// frame between two flags, filled up with the flag pattern), the check bytes, flagsAfter flags. LSB first as on the air. Empty when the frame is too
// long for one block.
std::vector<uint8_t> fx25Bits(const std::vector<uint8_t>& frameWithFcs, int checkBytes, int flagsBefore, int flagsAfter);
}

} // namespace dect2
