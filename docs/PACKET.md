# APRS / Packet (AX.25)

The APRS / Packet mode (standard 26, `core/src/packet/`) receives AX.25 packet radio on one FM channel: 1200 baud Bell 202 AFSK (APRS on
144.800 MHz in Europe, 144.390 MHz in North America) and 9600 baud G3RUH FSK at the same time. LoRa APRS (433.775 MHz) is a different radio
system and lives in the Mesh mode.

## What is supported

| Part | What it does |
| --- | --- |
| Input | Radio IQ from any SDR at 240 ksps or more (Airspy HF+, SpyServer and rtl_tcp at a low rate work; 2 Msps is the default), a sound card carrying IQ, or a sound card carrying FM-demodulated audio (Input: FM audio). |
| Channel | 25 kHz (default) or 12.5 kHz filter. The radio is tuned 50 kHz above the channel, away from its DC spike. AFC follows a carrier up to 12 kHz off (4 kHz in the 12.5 kHz filter), so a cheap 50 ppm radio still decodes. |
| 1200 baud | Three tone-correlator pairs (rectangular and Hann windows over one bit, and a shorter one) with different clock-loop speeds. Each pair has six slicers: five mark / space weights (0, -3, +3, -6, +6 dB) and one with an AGC per tone. Every slicer has its own clock loop and HDLC receiver ("multi-slicer"); a frame decoded by several of them is kept once. |
| Emphasis | Auto (default): no filter, the slicers cover a tone balance of +-6 dB. De-emphasis: for stations that send through a microphone input with pre-emphasis (the 2200 Hz tone louder). Pre-emphasis: for a sound card on a radio's speaker (de-emphasised audio, the 1200 Hz tone louder). The measured balance of the last frames is in the Emphasis tooltip (`PacketTelemetry::twistDb`). |
| 9600 baud | Two slicers with different filters, peak-tracking threshold, clock loop, the G3RUH descrambler (x^17 + x^12 + 1) and NRZI. |
| Fix bits | Off by default. 1 bit: a frame that fails its FCS is tried again with one wrong demodulator decision corrected (after NRZI that is two neighbouring bits; at 9600 baud also the descrambler's copies 12 and 17 bits later). 2 bits: also two wrong decisions in a row. Only while a carrier is present, at most three tries per frame, and only frames that look like APRS (capital-letter callsigns, the source with a digit, a UI frame with PID F0, no control characters in the text). A repaired frame shows "[1 bit fixed]". With one wrong decision and CRC-16's minimum distance of 4 a wrong frame cannot pass; with three or more wrong bits it can, rarely. |
| FX.25 / IL2P | Received at 1200 baud on the same demodulators, nothing to switch on; see "FX.25" below. A frame that came with FEC shows "[FX.25]" or "[IL2P]" (and the bytes repaired) in Packets > Log. |
| Carrier / DCD | 20 ms power blocks against the noise floor (the 10th percentile of the last 5 s): a block 3 dB over it is a carrier. Failed frames are only counted while a carrier is present (noise between two random flags is not a failure). |
| Frames | HDLC flags, bit stuffing, abort, FCS (CRC-16-CCITT). Frames list: newest last, 200 kept; stations: 300. A packet heard again within 30 s with only the path changed (a digipeater) is listed as "(again, via ...)" but not counted, gated to APRS-IS or sent to automations again; KISS clients get every frame. |
| APRS | Positions (plain, with timestamp, compressed, ambiguity), Mic-E, objects and items (killed too), status, messages, acks / rejects (reply-ack "{MM}AA" too), weather (with and without a position), telemetry, capabilities, queries, raw NMEA ($GPRMC / $GPGGA: the position), a '!' position after up to 40 characters of other text, and third-party packets (the station inside is listed with its position, "via" the gateway). |
| Map | Every station with a position; objects under their own name. |
| Outputs | KISS TCP server (port 8001, receive only: frames sent by a client are not transmitted) and the APRS-IS iGate (needs a callsign; passcode optional; NOGATE / RFONLY / TCPIP / TCPXX / q-constructs and third-party packets are not gated) in Settings > Data feeds. |
| Whole span | With whole-span decoding on, one receiver per 25 kHz carrier in the capture (wide_modes.cpp); the channel width, fix bits and emphasis reach every carrier receiver. |
| Transmit | The TX page builds an AX.25 UI frame (callsign, destination APRS, path WIDE1-1,WIDE2-1 or your own, at most eight digipeaters) with a position (rounded to 0.01 minute) or a status text, at 1200 or 9600 baud. |

## Settings (Packets tab)

- **Channel**: 25 kHz or 12.5 kHz (saved as `packetNarrow`). 12.5 kHz rejects a neighbouring channel better and also helps a weak signal by about 3 dB.
- **Emphasis**: Auto, De-emphasis, Pre-emphasis (`packetEmphasis`).
- **Fix bits**: Off, 1 bit, 2 bits (`packetFixBits`).
- **Input**: Radio IQ or FM audio (`packetAudioIn`). For FM audio choose the sound card as the radio (docs/SOUND-CARD.md). The discriminator output (flat audio) decodes 1200 and 9600 baud; a speaker output only 1200 baud. With an unsquelched radio the carrier detector sees noise all the time, so failed frames are not counted and fix bits does not run.

## Measured (tests/packet/test_packet_tnc.cpp)

A signal model written independently of the generator: 60 frames with 27 to 116 characters of text, TXDELAY 25 flags, random gaps; FM at 192 ksps
(the whole-span carrier rate) with complex noise. CNR = carrier to noise in 25 kHz. Decoded percentage (`test_packet_tnc --sweep`):

| Case | 6 dB | 8 dB | 10 dB | 12 dB+ |
| --- | --- | --- | --- | --- |
| 1200, flat audio, 3.5 kHz deviation | 27 | 100 | 100 | 100 |
| 1200, pre-emphasis (microphone input) | 0 | 83 | 100 | 100 |
| 1200, 2.5 kHz deviation | 0 | 90 | 100 | 100 |
| 1200, 5 kHz deviation | 95 | 100 | 100 | 100 |
| 1200, +1.5 kHz off / +7 kHz off | 20 / 25 | 100 | 100 | 100 |
| 1200, pre-emphasis, -1.5 kHz off | 0 | 80 | 100 | 100 |
| 1200, pre-emphasis, -500 ppm baud clock | 2 | 92 | 100 | 100 |
| 1200, 12.5 kHz filter (centred / -3 kHz / +5 kHz off) | 97 / 93 / 92 | 100 | 100 | 100 |
| 1200, tones 9 dB apart (mark low) | 2 | 78 | 100 | 100 |
| 1200, tones 6 dB apart (mark high) | 3 | 87 | 100 | 100 |
| 1200, pre-emphasis, -4 dB, 300-2700 Hz band-pass | 0 | 93 | 100 | 100 |
| 1200, pre-emphasis and limiter (4.5 kHz) | 78 | 98 | 100 | 100 |
| 1200, 5% of frames hit by a 10 dB weaker station | 28 | 93 | 100 | 100 |
| 1200, fix bits 1 | 52 | 100 | 100 | 100 |
| 9600, 3 kHz deviation | 2 | 35 | 93 | 100 |
| 9600, 3 kHz, +1.5 kHz off, +200 ppm | 0 | 28 | 93 | 100 |
| 9600, 2.5 kHz deviation | 0 | 7 | 82 | 98 (100 from 14) |
| 9600, 4 kHz, 12.5 kHz filter | 25 | 98 | 100 | 100 |

Before the multi-slicer (one slicer per correlator pair) the 8 dB column read: flat 97, pre-emphasis 73, 2.5 kHz 75, tones 9 dB apart 68,
6 dB apart 70, band-pass 77, -500 ppm with pre-emphasis 77. The FM demodulator's threshold is near 8 to 10 dB CNR, so these figures are in
the range of a good hardware or software TNC on the same signal. The checks without `--sweep` (CI) hold a margin under these numbers, plus:
FM audio input (speaker audio with the mark 5 dB up, discriminator audio with pre-emphasis, 9600 baud discriminator audio), fix bits on a
frame-free FM voice carrier (no frames), and fix bits at 7 dB (more frames than without it). Synthetic only: not yet compared on a real
recording such as the WA8LMF TNC test CD.

## FX.25

FX.25 (Stensat Group, 2006) adds Reed-Solomon forward error correction to AX.25 (`core/src/packet/packet_fec.cpp`): the normal AX.25 frame
(flags, bit stuffing, FCS) sits unchanged in one Reed-Solomon block, announced by a 64-bit correlation tag, and goes through NRZI like any
frame. A receiver without FX.25 still reads the frame when it arrives without errors; OnAir lists such a frame once, marked [FX.25] (the FEC
copy comes later, after the padding and the check bytes, and is matched to the plain one). Safe against wrong repairs: Karn's Reed-Solomon
decoder can return "0 errors" for a block that is not a codeword, so every decode re-encodes the parity and compares; the bytes that are
never sent must stay zero; and the repaired block must still hold a frame with a good FCS. The built-in test signal (modeOpt[0] = 3) sends
an FX.25 frame.

Measured with `build/test_packet_fecmodem --sweep` (one APRS position frame of 111 bytes with its FCS, 30 times, carrier to noise in
25 kHz, 240 ksps, 400 Hz off): frames through, of 30.

| 1200 baud | 3 dB | 4 dB | 5 dB | 6 dB | 7 dB |
| --- | --- | --- | --- | --- | --- |
| AX.25 | 1 | 6 | 25 | 29 | 30 |
| FX.25 | 27 | 30 | 30 | 30 | 30 |

Checked against Dire Wolf 1.8.1: 20 frames of its `gen_packets -X 1` (1200 baud, rising noise) through the FM-audio input: 15, each listed
once (plain AX.25 from the same tool: 14).

## Files

- `core/src/packet/packet_rx.cpp`: the receiver (front end, demodulators, frame handling, stations).
- `core/include/dect2/packet/packet_demod.h`, `packet_demod.cpp`: FM front end, `AfskMulti` (multi-slicer), `Emphasis`, `FskSlicer`.
- `core/src/packet/packet_fec.cpp`, `packet_fec.h`: FX.25 and IL2P receivers, and the FX.25 encoder the test signal uses.
- `core/src/packet/packet_ax25.cpp`: frame building, HDLC raw-bit capture and fix bits; `packet_ax25_rs.cpp` and `rust/onair_ax25`: FCS, frame parser, HDLC receiver.
- `core/src/packet/packet_aprs_full.cpp`: the extra APRS rules on top of the Rust parser (`rust/onair_ax25/src/aprs*.rs`).
- Tests: `tests/packet/test_packet_fec.cpp` (FX.25 / IL2P bit level), `test_packet_fecmodem.cpp` (FX.25 through the modem, `--sweep`), `tests/packet/test_packet_tnc.cpp` (benchmark), `test_packet_layers.cpp` (fix bits, parser rules), `test_packet_decode.cpp`, `test_packet_impair.cpp`, `test_packet_width.cpp`, `test_packet_wide.cpp`, `test_packet_engine.cpp`, `test_packet_proto*.cpp`.
