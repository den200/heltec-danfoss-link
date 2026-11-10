> Historical investigation notes. Start with the current [documentation index](docs/INDEX.md) for 0.13.1 behavior and operating instructions.

# Danfoss Link CC: findings (2026-10-20)

Current protocol report and phased implementation plan: [PROTOCOL-AND-PLAN.md](PROTOCOL-AND-PLAN.md).

## The CC
- Mk IV (Wi-Fi) at <cc-ip>, MAC <cc-mac>. Every TCP/UDP port is filtered; it only talks outbound to Trifork's MDG relay (`mdg-danfoss.hosted.trifork.com:443`).
- Firmware 4.2.1797 (`Danfoss Link CC 4.2.1797/`) = Danfoss's current download (manifest: requires 4.x.x).
  "Check for update" finds nothing because Mk IV has no OTA. Install via FAT32 USB stick, files in the root,
  House control → Settings → Wi-Fi and apps → App options → Software update → Update via USB.
- `ccimage.bin`: Freescale i.MX23/28 SB v1.1 image, 1 key, not the zero key (CBC-MAC mismatch), entropy ≈ 8.0 throughout.
  Encrypted with the CC's OTP key and the manifest is signed (`manifest.sig`, 128 B): no custom firmware without hardware access.
- `zensys_firmware.hex`: Sigma/Zensys 8051 Z-Wave Serial API, "Z-Wave 3.52". `extern_eep.hex`: its blank NVM ("ZeNsYs").

## Radio (sniffed with the rpi5 Heltec, `tools/zwdecode.py`)
- Z-Wave R2, 40 kbps, 868.40 MHz, data bit-inverted. Home ID `ffd2bb2d`, CC = node 1, heads seen: 2, 5, 6, 8, 12.
- Heads report every ~4m50s; the CC replies right away. Everything is Manufacturer Proprietary: `91 0002 10 <len> <body> <2 B>`.
- Head report body (len 0x28):
  `[0x10|seq] 0c <id?> 4720202021030020102011030f031703280318032f0330 <T1> <T2> <T3> <X> <valve> <Z> <4 B tag, 2 inside len + 2 after>`
  - T1–T3: °C × 100 (e.g. 0650 = 16.16)
  - valve: ‰ (03e8 = 100.0 %, 0159 = 34.5 %)
  - X: unknown (198, 260, 349, 179)
  - Z: unknown (3c, 40, 34, fe)
- CC reply body (len 0x0a): `[0x08|seq] 0d 000000<mode> <setpoint °C×100> <4 B tag>`.
  Setpoint confirmed 2026-10-20 22:56:31: 23.5 °C set on the CC for node 2 → `092e`, and mode went 05 → 06 (01 on nodes 5 and 8; meaning unknown,
  manual override vs schedule is a guess).
- Second CC → head message type (len 0x10), seen once (node 2, 22:51:40): `0d0d 002a 0e17540a1a00012c0014 <4 B tag>`. Possibly clock/schedule; `0a 1a` might be month 10, year 26.
- seq is 3 bits and wraps (node 8: 16, 17, 10). The reply echoes the head's seq.
- The tag is not a CRC: no CRC-16 fits (all polys, with or without the Z-Wave header), no CRC-32 fits, and it fails the linearity test
  (equal-delta reply pairs give different tag XORs). Most likely a MAC with a network key, so spoofing needs the key.

## Pairing capture: corrected audit (2026-10-20)
User confirms **one physical head** was paired. The previous claim that two spare heads
joined was unsupported. See `data/CAPTURE-AUDIT.md` for raw-row evidence and limitations.

- 23:27:53–54: bidirectional setup traffic for node 14, ending in proprietary op=90
  exchanges and unanswered captured NO_OPERATION retries.
- 23:28:22–25: similar setup for node 15, followed by routing and further configuration.
  One physical head going through an aborted/restarted setup is plausible; identity is not proven.
- 23:29:18–23: node **3**, not 15, receives op=90 `03 22c9dd0983` and repeated
  NO_OPERATION probes, with no captured response. This is consistent with the user's
  removal of an already-dead head. Proprietary removal semantics remain unverified.
- The 16-byte value `<network-key>` is indeed sent to both
  node addresses. It was a **candidate key** at this audit stage (subsequently verified below), not evidence of
  two different heads. For op=80 the next byte is not a general length: `04` precedes
  16 bytes, while `02` and `05` each precede one byte. A subcommand interpretation fits.
- No Assign IDs or Node Information Frame is recovered in the known-Home-ID audit.
  The protocol commands previously called assign-ID/node-info are range/routing commands.
- Original saved extract retained unchanged at `data/data-inclusion-nodes14-15.txt`.
  Raw receiver-window export: `data/raw-20261020-2320-2350.jsonl`.
  Complete known-network re-decode: `data/redecoded-20261020.txt`.
- The old decoder returned the first checksum-valid alignment, often an ACK later in
  a window, and missed earlier data. `tools/audit_capture.py` scans all alignments for
  the known Home ID: 93 frame occurrences versus 73 with the old decoder.

## Authentication SOLVED from pairing (2026-10-20)

The 16-byte pairing value is verified as a working authentication key for this network:
`<network-key>`.

- Cipher: XTEA, 32 cycles, big-endian 32-bit words.
- Construction: CBC-MAC, zero IV, input begins at proprietary operation byte
  (after `91 0002`), includes next length byte and body, excludes existing tag.
- Padding: when unaligned only, append `80` and zero-pad to 8 bytes. No extra block
  for already-aligned messages.
- Tag: last four bytes of the final encrypted block.
- 113 distinct captured frames (123 occurrences), including 54 held-out frames / 38
  new application messages, verify with no failures. Covers op=10, 50 and 90 and
  nodes 2,3,5,6,8,12,14,15,16 communicating with CC node 1.
- Independent C reference matches all 113; wrong-key and modified-message checks fail.
- The previously hypothesized hidden nonce/counter is NOT needed to reproduce these
  tags. Sequence bytes inside the vendor message are covered by the MAC; Z-Wave
  addresses and header are not included in this observed construction.
- Local control demonstrated 2026-10-21 at 00:19:13: timed Heltec ACK + authenticated
  extended reply set spare node 16 to 28 C with CC unplugged; user confirmed display.
  One-shot arm then disarmed; later unanswered reports explain the likely connection
  warning recurrence. Persistent controller and replay policy remain unimplemented/untested.
- Implementation: `tools/danfoss_auth.py`; full evidence: `data/AUTHENTICATION.md`.

## Paths to Home Assistant
1. Cloud: HACS `Ole-JH/ha-danfoss-link` (MDG relay, pairing code on the CC). Needs 4.2.1797 (TLS 1.2). Danfoss support is promised until 31 Dec 2026.
2. Local read-only: a dedicated 868 MHz FSK receiver decoding head reports → MQTT (temps, valve, setpoint).
3. Local control: key/tag verified AND one timed spare-head command accepted (28 C).
   Next engineering work: sustained report/ACK/reply handling, local-button changes,
   state persistence, per-head configuration, retries, and MQTT/Home Assistant integration.
