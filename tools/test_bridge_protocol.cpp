// Host-side regression test for the shared bridge protocol module.
// Usage: DANFOSS_NETWORK_KEY="$(security find-generic-password -s danfoss-link-network-key -w)" \
//          test_bridge_protocol < validated-frames.txt
// The key comes from the environment, never argv (visible in ps and shell history).
// Exit 0 on success; distinct nonzero codes identify the failing check.
#include "../firmware-bridge/src/danfoss_protocol.h"
#include "../firmware-bridge/src/registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t HOME[4] = {0xff, 0xd2, 0xbb, 0x2d};

static size_t unhex(const char *s, uint8_t *out) {
  unsigned v; size_t n = strlen(s);
  while (n && (s[n-1] == '\n' || s[n-1] == '\r')) n--;
  n /= 2;
  for (size_t i = 0; i < n; i++) { sscanf(s + 2*i, "%2x", &v); out[i] = v; }
  return n;
}
static bool eqhex(const uint8_t *f, size_t n, const char *expect) {
  uint8_t e[64]; size_t en = unhex(expect, e);
  return en == n && !memcmp(f, e, n);
}

int main(int argc, char **) {
  const char *hexKey = getenv("DANFOSS_NETWORK_KEY");
  if (argc != 1 || !hexKey || strlen(hexKey) != 32) return 2;
  uint8_t k[16]; unsigned v;
  for (int i = 0; i < 16; i++) { sscanf(hexKey + 2*i, "%2x", &v); k[i] = v; }

  // 1. MAC verifies on every captured validated frame.
  char line[2048]; int checked = 0, replayed = 0, truncatedContexts = 0;
  while (fgets(line, sizeof line, stdin)) {
    uint8_t f[1024]; size_t n = unhex(line, f);
    if (!n) continue;
    uint8_t tag[4]; Danfoss::mac(f+12, n-17, k, tag);
    if (memcmp(tag, f+n-5, 4)) return 3;
    if (f[4] == 1 && f[5] == 0x41 && !(f[6] & 0xf0) && (f[12] == 0x10 || f[12] == 0x50) &&
        (f[14] & 0xf8) == 8 && f[15] == 0x0d && (n == 27 || n == 37)) {
      uint8_t reproduced[64]; size_t reproducedLen = 0;
      if (n == 27) reproducedLen = Danfoss::buildShortReply(HOME, f[8], f[6], f[14],
          Danfoss::replyContext(f), Danfoss::replySetpoint(f), k, reproduced, f[12]);
      else reproducedLen = Danfoss::buildExtendedReply(
          HOME, f[8], f[6], f[14], Danfoss::replyContext(f), Danfoss::replySetpoint(f), k, reproduced, f + 22, f[12]);
      if (reproducedLen) {
        if (reproducedLen != n || memcmp(reproduced, f, n)) return 48;
        ++replayed;
        if (Danfoss::replyContext(f) > 255) ++truncatedContexts;
      }
    }
    checked++;
  }
  fprintf(stderr, "bridge MAC verified on %d frames\n", checked);
  fprintf(stderr, "CC builder replay: %d frames; %d require context above byte19\n", replayed, truncatedContexts);

  // 2. Genuine head reports (both layouts) and CC replies validate any-node.
  uint8_t f[64];
  size_t n = unhex("ffd2bb2d10410139019100021028100c0d85030020102011030f031703280318032f033003010302030303230754006300955300a0115c617f", f);
  if (n != 57 || !Danfoss::validFrame(f, n, k, HOME)) return 4;
  uint8_t sf[64];
  size_t sn = unhex("ffd2bb2d10410235019100021024110c0d46030020102011030f0317030103280318032f033007a1006300b7520008344d389ad4c2", sf);
  if (sn != 53 || !Danfoss::validFrame(sf, sn, k, HOME)) return 5;
  if (Danfoss::reportTarget(sf, sn) != 2100) return 6;  // button change to 21 C

  // 3. Rejections: wrong Home ID, flipped authenticated byte, bad length field,
  //    truncated frame, valid frame with wrong key.
  uint8_t bad[57]; memcpy(bad, f, 57);
  bad[0] ^= 1; if (Danfoss::validFrame(bad, 57, k, HOME)) return 7;
  memcpy(bad, f, 57);
  bad[16] ^= 1; bad[56] = Danfoss::crc(bad, 56);
  if (Danfoss::validFrame(bad, 57, k, HOME)) return 8;
  memcpy(bad, f, 57);
  bad[7] = 56; bad[56] = Danfoss::crc(bad, 56);
  if (Danfoss::validFrame(bad, 57, k, HOME)) return 9;
  if (Danfoss::validFrame(f, 56, k, HOME)) return 10;
  uint8_t wrongKey[16] = {0};
  if (Danfoss::validFrame(f, 57, wrongKey, HOME)) return 11;

  // 4. Reply builders reproduce recorded genuine/proven frames byte-for-byte.
  uint8_t out[64];
  size_t on = Danfoss::buildAck(HOME, 16, 1, out);
  if (!eqhex(out, on, "ffd2bb2d0103010a105d")) return 12;  // captured bridge ACK
  on = Danfoss::buildShortReply(HOME, 16, 4, 6, 0, 1700, k, out);
  if (!eqhex(out, on, "ffd2bb2d0141041b10910002100a0e0d0000000006a4904fe4b3ab")) return 13;  // genuine CC 17 C
  const uint8_t old28Configuration[] = {0x0a,0xf0,2,0x58,0x0a,0xf0,0,0,0x0a,0xf0};
  on = Danfoss::buildExtendedReply(HOME, 16, 10, 0, 3, 2800, k, out, old28Configuration);
  if (!eqhex(out, on, "ffd2bb2d01410a25109100021014080d000000030af00af002580af000000af0a12d51a08d")) return 14;  // proven 28 C

  // Genuine CC configuration must survive an unrelated target change byte-for-byte.
  const uint8_t ccConfiguration[] = {0x06,0xa4,2,0x58,0x0a,0xf0,0,0,0x0a,0xf0};
  on = Danfoss::buildExtendedReply(HOME, 15, 3, 2, 1, 1700, k, out, ccConfiguration);
  if (!eqhex(out, on, "ffd2bb2d014103250f91000210140a0d0000000106a406a402580af000000af09eb4957920")) return 41;
  on = Danfoss::buildExtendedReply(HOME, 15, 4, 3, 1, 1900, k, out, ccConfiguration);
  if (!Danfoss::validFrame(out, on, k, HOME) || Danfoss::replySetpoint(out) != 1900 ||
      memcmp(out + 22, ccConfiguration, sizeof ccConfiguration)) return 42;
  uint8_t manualConfiguration[10]; memcpy(manualConfiguration, ccConfiguration, 10);
  manualConfiguration[0] = 0x07; manualConfiguration[1] = 0x6c;
  on = Danfoss::buildExtendedReply(HOME, 16, 15, 7, 2, 1900, k, out, manualConfiguration);
  if (!eqhex(out, on, "ffd2bb2d01410f251091000210140f0d00000002076c076c02580af000000af00fe5cf75a3")) return 44;
  if (Danfoss::buildExtendedReply(HOME, 15, 4, 3, 1, 1900, k, out, nullptr) ||
      Danfoss::buildExtendedReply(HOME, 15, 4, 3, 1, 2801, k, out, ccConfiguration)) return 43;

  // 5. Observation helpers on the genuine CC reply.
  uint8_t cc[64];
  size_t cn = unhex("ffd2bb2d0141041b10910002100a0e0d0000000006a4904fe4b3ab", cc);
  if (!Danfoss::validFrame(cc, cn, k, HOME)) return 15;
  if (Danfoss::replySetpoint(cc) != 1700 || Danfoss::replyContext(cc) != 0) return 16;
  // Held-out real CC frame: bit8 is present and must not be silently discarded.
  uint8_t flagged[64];
  size_t flaggedLen = unhex("ffd2bb2d0141021b06910002100a0d0d000001010834d6099827ce", flagged);
  if (!Danfoss::validFrame(flagged, flaggedLen, k, HOME) || Danfoss::replyContext(flagged) != 0x101) return 45;
  on = Danfoss::buildShortReply(HOME, 6, 2, 5, 0x101, 2100, k, out);
  if (on != flaggedLen || memcmp(out, flagged, on)) return 46;
  on = Danfoss::buildExtendedReply(HOME, 6, 3, 6, 0x101, 1900, k, out, ccConfiguration);
  if (Danfoss::replyContext(out) != 0x101 || !Danfoss::validFrame(out, on, k, HOME) ||
      memcmp(out + 24, ccConfiguration + 2, 8)) return 47;

  // Node5's authenticated short setpoint poll must not be dropped as unknown.
  uint8_t poll[22];
  size_t pn = unhex("ffd2bb2d05410116019100021005090d0030e7764570", poll);
  if (!Danfoss::validFrame(poll, pn, k, HOME) ||
      Danfoss::reportKind(poll, pn) != Danfoss::ReportKind::Poll ||
      Danfoss::reportStatusRaw(poll, pn) != 0) return 49;
  Bridge::Head polling; polling.node = 5;
  polling.observe(poll, pn, 100);
  if (!polling.hasLast || polling.roomKnown || polling.statusKnown) return 50;

  // 6. Link ACK shape: a genuine reply frame is not an ACK, a built ACK is.
  if (Danfoss::isAck(cc, cn, HOME)) return 17;
  on = Danfoss::buildAck(HOME, 16, 1, out);
  if (!Danfoss::isAck(out, on, HOME)) return 18;

  // 7. Report classification must inspect structure, not just frame length.
  if (Danfoss::reportKind(f, n) != Danfoss::ReportKind::Alternate ||
      Danfoss::reportKind(sf, sn) != Danfoss::ReportKind::Button ||
      Danfoss::reportKind(cc, cn) != Danfoss::ReportKind::Other) return 19;
  uint8_t low[64];
  size_t ln = unhex("ffd2bb2d06410135019100021024170c0546030020102011030f0317030103280318032f0330075d00b302a0fe0006a4e6aa923e09", low);
  if (!Danfoss::validFrame(low, ln, k, HOME) ||
      Danfoss::reportKind(low, ln) != Danfoss::ReportKind::Button ||
      Danfoss::reportTarget(low, ln) != 1700 ||
      Danfoss::reportStatusRaw(low, ln) != 0xfe00) return 20;
  uint8_t routine[64];
  size_t rn = unhex("ffd2bb2d06410139019100021028140c054720202021030020102011030f031703280318032f0330076407a5075600b30287fe001ea7efd425", routine);
  if (!Danfoss::validFrame(routine, rn, k, HOME) ||
      Danfoss::reportKind(routine, rn) != Danfoss::ReportKind::Routine ||
      Danfoss::reportStatusRaw(routine, rn) != 0xfe00) return 29;
  // Node 13 resumes after a battery replacement; CC uses op=50 for its target.
  uint8_t cc13[64];
  size_t c13n = unhex("ffd2bb2d0141081b0d910002500a0b0d000000010640b753ff4dc4", cc13);
  if (!Danfoss::validFrame(cc13, c13n, k, HOME) ||
      Danfoss::replySetpoint(cc13) != 1600 || Danfoss::replyContext(cc13) != 1) return 30;
  memcpy(bad, f, 57); bad[17] = 0x46;
  if (Danfoss::reportKind(bad, 57) != Danfoss::ReportKind::Other) return 21;
  memcpy(bad, f, 57); bad[8] = 2;
  if (Danfoss::reportKind(bad, 57) != Danfoss::ReportKind::Other) return 22;
  memcpy(bad, f, 57); bad[5] |= 0x80; bad[56] = Danfoss::crc(bad, 56);
  if (Danfoss::validFrame(bad, 57, k, HOME)) return 23;

  // 8. Sequence wrap alone is not a duplicate: payload must also match.
  Bridge::Head h;
  if (h.observe(low, ln, 100) || !h.observe(low, ln, 200)) return 24;
  low[38] ^= 1;
  if (h.observe(low, ln, 300) || h.observe(low, ln, 10400)) return 25;
  // A link ACK confirms delivery, and cannot consume a newer external command.
  h.pendingExtended = true;
  h.targetRevision = h.replyRevision = 1;
  h.replyLen = Danfoss::buildExtendedReply(HOME, 6, 10, 7, 2, 1700, k, h.replyFrame, ccConfiguration);
  h.awaitingAck = true; h.lastReplyMs = 11000;
  if (h.acknowledge(9, 11010) || h.acknowledge(10, 13000) || !h.pendingExtended) return 26;
  h.targetRevision = 2;
  if (!h.acknowledge(10, 11020) || !h.pendingExtended || h.acknowledge(10, 11021)) return 27;
  h.awaitingAck = true; h.replyRevision = 2;
  if (!h.acknowledge(10, 11030) || h.pendingExtended || h.replyAcks != 2) return 28;

  fprintf(stderr, "all bridge protocol checks passed\n");
  return 0;
}
