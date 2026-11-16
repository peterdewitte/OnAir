// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kristijonas Adomaitis and OnAir contributors

// FX.25 through the whole modem: the generator's FEC cycle (FX.25 at 1200 baud) into the APRS / Packet receiver
// with noise and a carrier offset, every frame once with its FEC named; then one frame repeated near the threshold as plain AX.25 and FX.25, where
// the Reed-Solomon codes must get more frames through than plain AX.25. Simulated signals only (fixed noise seed, so the counts do not change run to run).
#include "dect2/packet/packet_gen.h"
#include "dect2/packet/packet_rx.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
using namespace dect2;
#include "../common/test_util.h"

namespace {

constexpr double kRate = 240000;   // the receiver's minimum rate keeps the test short

PacketTelemetry feed(ModeSynth& syn, double sec, double rate = kRate) {
    PacketReceiver rx;
    rx.configure(rate);
    rx.setSignalOffset(-packetTuning().tuneOffsetHz);
    rx.setSilent(true);
    const size_t total = (size_t)(sec * rate);
    std::vector<cf32> buf(65536);
    for (size_t done = 0; done < total;) {
        const size_t n = std::min(buf.size(), total - done);
        syn.generate(buf.data(), n);
        rx.feed(buf.data(), n);
        done += n;
    }
    PacketTelemetry t;
    rx.telemetry(t, 0);
    return t;
}

const char* fecName(PacketFec f) { return f == PacketFec::Fx25 ? "FX.25" : ""; }

void cycle(double snrDb, double cfoHz) {
    const std::vector<PacketGenFrame> want = packetGenCycle(3);
    SynthConfig cfg;
    cfg.snrDb = snrDb; cfg.cfoHz = cfoHz; cfg.modeOpt[0] = 3;
    auto syn = makePacketSynth(cfg, kRate);
    CHECK(syn != nullptr, "no generator");
    if (!syn) return;
    const PacketTelemetry t = feed(*syn, packetGenCycleSec(3) + 0.35);
    int found = 0;
    for (const PacketGenFrame& g : want) {
        int hits = 0;
        std::string fec;
        for (const PacketFrameInfo& r : t.frames)
            if (r.from == g.frame.from.str() && r.to == g.frame.to.str() && r.path == g.frame.pathStr() && r.info == g.frame.info && r.baud == g.baud) { hits++; fec = r.fec; }
        if (hits == 1 && fec == fecName(g.fec)) found++;
        else printf("  %s at %d baud: decoded %d times, FEC \"%s\" (sent %s)\n", g.frame.tnc2().c_str(), g.baud, hits, fec.c_str(), fecName(g.fec));
    }
    printf("FEC cycle (%.0f dB, %+.0f Hz): %d of %zu frames, %zu decoded, FX.25 %llu, IL2P %llu, bad %llu\n", snrDb, cfoHz, found, want.size(), t.frames.size(),
           (unsigned long long)t.fx25Frames, (unsigned long long)t.il2pFrames, (unsigned long long)t.blocksBad);
    CHECK(found == (int)want.size(), "FEC cycle: %d of %zu frames", found, want.size());
    CHECK(t.frames.size() == want.size(), "FEC cycle: %zu frames listed, %zu sent", t.frames.size(), want.size());
    CHECK(t.fx25Frames == 1 && t.il2pFrames == 0, "FEC cycle: FX.25 %llu, IL2P %llu", (unsigned long long)t.fx25Frames, (unsigned long long)t.il2pFrames);
}

int wrongFrames = 0;   // frames that came out with other contents than were sent

// one APRS frame sent `reps` times at snrDb: how many came through with exactly the contents sent
int through(int baud, PacketFec fec, double snrDb, int reps) {
    PacketGenFrame g;
    g.frame.from.call = "A61FEC"; g.frame.from.ssid = 7; g.frame.to.call = "APRS";
    g.frame.info = "!2512.34N/05518.12E>A position report with a comment of usual length, sent near the threshold";
    g.baud = baud; g.fec = fec;
    SynthConfig cfg;
    cfg.snrDb = snrDb; cfg.cfoHz = 400;
    const double gap = 0.2;
    auto syn = makePacketSynthBurst(cfg, kRate, {g}, gap);
    if (!syn) return -1;
    const PacketTelemetry t = feed(*syn, reps * packetSynthBurstSec({g}, gap) + 0.3);
    int good = 0;
    for (const PacketFrameInfo& r : t.frames) {
        if (r.from == g.frame.from.str() && r.to == g.frame.to.str() && r.path.empty() && r.info == g.frame.info) good++;
        else { wrongFrames++; printf("  wrong frame (%d baud, %s, %.1f dB): %s>%s: %s\n", baud, fecName(fec), snrDb, r.from.c_str(), r.to.c_str(), r.info.c_str()); }
    }
    return good;
}

void gain(int baud, double snrDb, int reps) {
    const int plain = through(baud, PacketFec::None, snrDb, reps);
    const int fx = through(baud, PacketFec::Fx25, snrDb, reps);
    printf("%d baud at %.1f dB, %d frames sent: AX.25 %d, FX.25 %d\n", baud, snrDb, reps, plain, fx);
    CHECK(plain >= 0 && fx <= reps, "%d baud: counts out of range", baud);
    CHECK(fx >= plain + reps / 2, "%d baud at %.1f dB: FX.25 not clearly better than AX.25 (%d / %d)", baud, snrDb, plain, fx);
}

// 9600 baud frames close together, fed in large blocks: the two 9600 slicers take turns over a block, so one slicer's copy of a frame can come after
// the other's next frame; each frame must still be listed once (it was listed again as "again, via direct", plain and FX.25 alike)
void closeFrames(PacketFec fec) {
    std::vector<PacketGenFrame> gs;
    for (int i = 0; i < 6; i++) {
        PacketGenFrame g;
        g.frame.from.call = "A61FEC"; g.frame.from.ssid = i + 1; g.frame.to.call = "APRS";
        g.frame.info = ">frame " + std::to_string(i) + " of six, sent close together at 9600 baud";
        g.baud = 9600; g.fec = fec;
        gs.push_back(g);
    }
    SynthConfig cfg;
    cfg.snrDb = 20; cfg.cfoHz = 300;
    auto syn = makePacketSynthBurst(cfg, kRate, gs, 0.02);
    CHECK(syn != nullptr, "no generator");
    if (!syn) return;
    const PacketTelemetry t = feed(*syn, packetSynthBurstSec(gs, 0.02) * 0.999);
    int once = 0;
    for (const PacketGenFrame& g : gs) {
        int hits = 0;
        for (const PacketFrameInfo& r : t.frames) if (r.from == g.frame.from.str() && r.info == g.frame.info) hits++;
        if (hits == 1) once++;
        else printf("  %s: listed %d times\n", g.frame.from.str().c_str(), hits);
    }
    printf("9600 baud, 6 frames 20 ms apart (%s): %d listed once, %zu listed\n", fec == PacketFec::Fx25 ? "FX.25" : "AX.25", once, t.frames.size());
    CHECK(once == 6 && t.frames.size() == 6, "close 9600 baud frames: %d of 6 listed once, %zu listed", once, t.frames.size());
}

// --sweep: the table in docs/PACKET.md (frames of 30 through, by SNR)
void sweep() {
    for (int baud : {1200}) {
        printf("%d baud: dB, AX.25, FX.25 (of 30)\n", baud);
        for (double db = baud == 1200 ? 1.0 : 5.0; db <= (baud == 1200 ? 9.0 : 15.0); db += 1.0)
            printf("  %4.1f  %2d  %2d\n", db, through(baud, PacketFec::None, db, 30), through(baud, PacketFec::Fx25, db, 30));
    }
    printf("frames with wrong contents: %d\n", wrongFrames);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--sweep") { sweep(); return 0; }
    cycle(14.0, 600.0);
    cycle(20.0, -800.0);
    closeFrames(PacketFec::None);
    gain(1200, 4.0, 30);
    CHECK(wrongFrames == 0, "%d frames with wrong contents", wrongFrames);
    return testSummary("packet_fecmodem");
}
