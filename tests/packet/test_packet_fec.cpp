// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kristijonas Adomaitis and OnAir contributors

// FX.25 and IL2P receive: blocks built by this test's encoder following the tag table and spec, with byte errors up to the code's limit, inverted level for IL2P, and false-sync noise. Simulated bits only.
// FX.25 also through the FX.25 encoder (fx25Bits) and NRZI, next to a plain AX.25 receiver.
#include "dect2/packet/packet_ax25.h"
#include "dect2/packet/packet_fec.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
using namespace dect2;
using namespace dect2::packetfec;
#include "../common/test_util.h"
#undef CHECK
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static std::vector<uint8_t> ax25Frame() {
    // N0CALL-1 > APRS: "hello"
    std::vector<uint8_t> f;
    for (char c : std::string("APRS  ")) f.push_back((uint8_t)(c << 1));
    f.push_back(0xE0);
    for (char c : std::string("N0CALL")) f.push_back((uint8_t)(c << 1));
    f.push_back(0x63);
    f.push_back(0x03); f.push_back(0xF0);
    for (char c : std::string(">hello from the test")) f.push_back((uint8_t)c);
    return f;
}

static void fx25Test() {
    const auto frame = ax25Frame();
    std::vector<uint8_t> fwf = frame; ax25::appendFcs(fwf);
    // stuff bits with HDLC flags at both ends, LSB first; level bits per the HDLC transmit helper
    std::vector<uint8_t> bits = ax25::hdlcBits(fwf, 1, 1);
    const std::vector<uint8_t>& dataBits = bits;
    for (int tag : {3, 2, 7, 11}) for (int errs : {0, 1, 2, 3, 6}) {
        uint64_t v; int n, k;
        CHECK(fx25Tag(tag, v, n, k));
        std::vector<uint8_t> d((size_t)k, 0);
        size_t bi = 0;
        for (int i = 0; i < k && bi < dataBits.size(); i++) for (int b = 0; b < 8 && bi < dataBits.size(); b++, bi++) d[(size_t)i] |= (uint8_t)(dataBits[bi] << b);
        if (bi < dataBits.size()) { d.resize((size_t)k); continue; }   // the frame must fit
        const int nroots = n - k;
        std::vector<uint8_t> full(255 - (size_t)nroots, 0);
        std::memcpy(full.data(), d.data(), (size_t)k);
        std::vector<uint8_t> par((size_t)nroots);
        rsEncodeParity(full.data(), (int)full.size(), nroots, 1, par.data());
        std::vector<uint8_t> air = d;
        air.insert(air.end(), par.begin(), par.end());
        std::mt19937 g((unsigned)tag * 13 + (unsigned)errs);
        const int limit = nroots / 2;
        const int put = std::min(errs, limit);
        for (int e = 0; e < put; e++) air[(size_t)e * 7 % (size_t)n] ^= (uint8_t)(0x55 + e);
        PacketFecRx rx;
        std::vector<uint8_t> got; int rep = -1;
        rx.setSink([&](const PacketFecFrame& f) { CHECK(!f.il2p); got = f.ax25; rep = f.repaired; });
        int level = 0;
        for (int i = 0; i < 64; i++) { const int b = (int)((v >> i) & 1); level = b ? level : !level; rx.lineBit(level, false); }
        for (uint8_t by : air) for (int b = 0; b < 8; b++) { const int d1 = (by >> b) & 1; level = d1 ? level : !level; rx.lineBit(level, false); }
        CHECK(got == frame);
        CHECK(rep == put);
    }
    // a corrupted tag within tolerance still locks
}

static std::vector<uint8_t> il2pBytes(const std::vector<uint8_t>& payload, int errHdr, int errPay) {
    // Type 1 header: UI frame, N0CALL-1 > APRS-0
    uint8_t h[13] = {};
    const std::string dst = "APRS  ", src = "N0CALL";
    for (int i = 0; i < 6; i++) { h[i] = (uint8_t)(dst[(size_t)i] - 0x20); h[6 + i] = (uint8_t)(src[(size_t)i] - 0x20); }
    h[12] = (uint8_t)(0 << 4 | 1);
    h[0] |= 1 << 6;                                    // UI
    // PID 1 (U frame, omitted PID in AX.25 except UI): bits 3..0 in bytes 1..4 bit 6
    const int pid = 1;
    for (int i = 0; i < 4; i++) if (pid & (1 << (3 - i))) h[1 + i] |= 1 << 6;
    // control: UI opcode 5 (bits 4..2), command 1
    const int ctl = (5 << 3) | (1 << 2);
    for (int i = 0; i < 7; i++) if (ctl & (1 << (6 - i))) h[5 + i] |= 1 << 6;
    h[1] |= 0x80;                                      // type 1
    for (int i = 0; i < 10; i++) if ((int)payload.size() & (1 << (9 - i))) h[2 + i] |= 0x80;
    auto sh = il2pScramble(h, 13);
    uint8_t par[2];
    rsEncodeParity(sh.data(), 13, 2, 0, par);
    std::vector<uint8_t> o = {0xF1, 0x5E, 0x48};
    o.insert(o.end(), sh.begin(), sh.end());
    o.insert(o.end(), par, par + 2);
    const int count = (int)(payload.size() + 238) / 239;
    const int small = (int)payload.size() / std::max(count, 1), big = (int)payload.size() - count * small;
    size_t p = 0;
    for (int i = 0; i < count; i++) {
        const int n = i < big ? small + 1 : small;
        auto sb = il2pScramble(payload.data() + p, (size_t)n);
        uint8_t pr[16];
        rsEncodeParity(sb.data(), n, 16, 0, pr);
        o.insert(o.end(), sb.begin(), sb.end());
        o.insert(o.end(), pr, pr + 16);
        p += (size_t)n;
    }
    if (errHdr) o[4] ^= 0x10;                          // one byte error in the scrambled header
    for (int e = 0; e < errPay; e++) o[20 + (size_t)e] ^= (uint8_t)(0x21 + e);
    return o;
}

static void il2pTest() {
    std::vector<uint8_t> payload; for (char c : std::string(">hello from the test IL2P packet")) payload.push_back((uint8_t)c);
    for (int inv = 0; inv < 2; inv++) for (int ep : {0, 8}) for (int eh : {0, 1}) {
        auto bytes = il2pBytes(payload, eh, ep);
        PacketFecRx rx;
        std::vector<uint8_t> got; bool isIl = false;
        rx.setSink([&](const PacketFecFrame& f) { got = f.ax25; isIl = f.il2p; });
        std::mt19937 g(1);
        for (int i = 0; i < 80; i++) rx.lineBit((int)(g() & 1) ^ inv, true);   // junk before the sync
        for (uint8_t b : bytes) for (int i = 7; i >= 0; i--) rx.lineBit(((b >> i) & 1) ^ inv, true);
        CHECK(isIl);
        // AX.25 address, control 0x03 (UI, with PID F0), payload
        std::vector<uint8_t> frame;
        for (char c : std::string("APRS  ")) frame.push_back((uint8_t)(c << 1));
        frame.push_back(0xE0);
        for (char c : std::string("N0CALL")) frame.push_back((uint8_t)(c << 1));
        frame.push_back(0x63);
        frame.push_back(0x03); frame.push_back(0xF0);
        frame.insert(frame.end(), payload.begin(), payload.end());
        CHECK(got == frame);
    }
}

static std::vector<uint8_t> ax25Ui(const std::string& info, bool withPath = false) {
    ax25::Frame f;
    f.to.call = "APRS"; f.to.h = true; f.from.call = "N0CALL"; f.from.ssid = 1;   // an AX.25 v2 command, as IL2P Type 1 gives it back
    if (withPath) { ax25::Address a; a.call = "WIDE2"; a.ssid = 2; f.path.push_back(a); }
    f.info = info;
    std::vector<uint8_t> b = ax25::buildFrame(f);
    b.resize(b.size() - 2);
    return b;
}

// FX.25 encoder -> NRZI -> receiver, with byte errors in the block; a plain AX.25 receiver reads the same frame
static void fx25RoundTrips() {
    for (const std::string& info : {std::string(">short"), std::string(60, 'm'), std::string(180, 'L')}) {
        const std::vector<uint8_t> fr = ax25Ui(info);
        std::vector<uint8_t> fwf = fr;
        ax25::appendFcs(fwf);
        for (int check : {0, 16, 32, 64}) {
            const std::vector<uint8_t> bits = fx25Bits(fwf, check, 4, 2);
            if (bits.empty()) { CHECK(info.size() > 100 && check != 0); continue; }   // the long frame does not fit some families' blocks
            const size_t start = 4 * 8 + 64, blockBytes = (bits.size() - 6 * 8 - 64) / 8;
            const int maxErr = check == 64 ? 32 : check == 32 ? 16 : 8;
            for (int errs : {0, 1, 8}) {
                if (errs > maxErr) continue;
                std::vector<uint8_t> b = bits;
                for (int e = 0; e < errs; e++) { const size_t byte = (size_t)e * 13 % blockBytes; b[start + byte * 8 + 3] ^= 1; }
                const std::vector<uint8_t> lv = ax25::nrziEncode(b);
                PacketFecRx rx;
                std::vector<PacketFecFrame> got;
                rx.setSink([&](const PacketFecFrame& f) { got.push_back(f); });
                std::vector<uint8_t> plain;
                ax25::HdlcRx h([&](const std::vector<uint8_t>& d, bool ok) { if (ok) plain = d; });
                for (uint8_t l : lv) { rx.lineBit(l, false); h.bit(l); }
                CHECK(got.size() == 1);
                if (got.size() == 1) { CHECK(got[0].ax25 == fr); CHECK(got[0].repaired == errs); CHECK(!got[0].il2p); CHECK(got[0].lateBits > 0); }
                if (errs == 0) CHECK(plain == fr);
            }
        }
    }
    CHECK(fx25PickTag(0, 20) == 4);
    CHECK(fx25PickTag(0, 100) == 6);
    CHECK(fx25PickTag(16, 100) == 2);
    CHECK(fx25PickTag(0, 240) == 0);
}

// a block whose decoder "repairs" the bytes that are never sent (zeros between the data and the check bytes) is not a frame
static void fx25NeverSent() {
    std::vector<uint8_t> fr = ax25Ui(">short");
    ax25::appendFcs(fr);
    const std::vector<uint8_t> bits = fx25Bits(fr, 0, 2, 2);
    CHECK(!bits.empty());
    // a clean block decodes; then the same block with its parity replaced by that of a block with a byte in the unsent zeros: nothing
    uint64_t v; int n = 0, k = 0;
    CHECK(fx25Tag(fx25PickTag(0, 20), v, n, k));
    std::vector<uint8_t> full((size_t)(255 - (n - k)), 0);
    for (int i = 0; i < k; i++) for (int b = 0; b < 8; b++) full[(size_t)i] |= (uint8_t)(bits[(size_t)(2 * 8 + 64 + i * 8 + b)] << b);
    full[(size_t)k + 3] = 0x5A;
    std::vector<uint8_t> par((size_t)(n - k));
    rsEncodeParity(full.data(), (int)full.size(), n - k, 1, par.data());
    std::vector<uint8_t> b = bits;
    for (int i = 0; i < n - k; i++) for (int j = 0; j < 8; j++) b[(size_t)(2 * 8 + 64 + (k + i) * 8 + j)] = (uint8_t)((par[(size_t)i] >> j) & 1);
    PacketFecRx rx;
    int got = 0;
    rx.setSink([&](const PacketFecFrame&) { got++; });
    for (uint8_t l : ax25::nrziEncode(b)) rx.lineBit(l, false);
    CHECK(got == 0);
}

int main() {
    fx25Test();
    fx25RoundTrips();
    fx25NeverSent();
    il2pTest();
    { PacketFecRx rx; int n = 0; rx.setSink([&](const PacketFecFrame&) { n++; }); std::mt19937 g(3); for (int i = 0; i < 200000; i++) rx.lineBit((int)(g() & 1), true); CHECK(n == 0); }
    return testSummary("packet_fec");
}
