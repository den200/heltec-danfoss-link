// Portable acceptance scenarios for the candidate manual-ID state machine.
// These prove controller state transitions, not the physical head's behavior.
#include "../firmware-bridge/src/persistent_state.h"
#include <assert.h>
#include <stdio.h>

static const uint8_t HOME[] = {0xff,0xd2,0xbb,0x2d}, KEY[16] = {};
static const uint8_t CONFIG[] = {0x06,0xa4,2,0x58,0x0a,0xf0,0,0,0x0a,0xf0};

static Bridge::Head head(uint32_t context = 1) {
  Bridge::Head h;
  h.node = 16; h.present = h.enabled = true;
  h.targetKnown = h.acceptedKnown = h.configurationKnown = true;
  h.targetC100 = h.acceptedTargetC100 = 1700;
  memcpy(h.configuration, CONFIG, sizeof CONFIG);
  h.learnContext(context, 1700);
  return h;
}

static void issue(Bridge::Head &h, uint8_t sequence, uint32_t now) {
  assert(h.prepareReply());
  uint8_t configuration[10]; memcpy(configuration, h.configuration, 10);
  configuration[0] = h.targetC100 >> 8; configuration[1] = h.targetC100;
  h.replyLen = Danfoss::buildExtendedReply(HOME, h.node, sequence, 1,
      h.replyContextRaw, h.targetC100, KEY, h.replyFrame, configuration);
  assert(h.replyLen == 37 && !memcmp(h.replyFrame + 24, CONFIG + 2, 8));
  h.contextUnsent = false; // as answerReport does once a reply carries the ID
  h.replyRevision = h.targetRevision;
  h.awaitingAck = true; h.lastReplyMs = now;
}

int main() {
  auto h = head();
  // Prospective real-CC observation: local17→22, ID1→2; reports retain2.
  assert(h.localTarget(2200, 100) && h.replyContextRaw == 2 && !h.pendingExtended);
  assert(h.localTarget(2200, 101) && h.replyContextRaw == 2);
  assert(h.prepareReply() && h.replyContextRaw == 2);
  // SHADOW does not race the real CC's ID allocation.
  h = head(); h.enabled = false;
  assert(h.localTarget(2200, 100) && h.replyContextRaw == 1);

  // Firmware path: every SET binds at once. Changes before the next wake rebind
  // the same unsent ID (even back to the accepted target); once a reply carried
  // it, a different target needs the next ID.
  h = head(0x01020304);
  assert(h.requestTarget(1900, 100) && h.prepareReply() && h.replyContextRaw == 0x01020305);
  assert(h.requestTarget(2000, 101) && h.prepareReply() && h.replyContextRaw == 0x01020305);
  assert(h.contextTargetC100 == 2000 && !h.replyDurable());
  assert(h.requestTarget(1700, 102) && h.prepareReply() && h.replyContextRaw == 0x01020305);
  assert(h.requestTarget(2000, 103)); issue(h, 3, 110);
  assert(h.replyContextRaw == 0x01020305 && h.contextTargetC100 == 2000);
  assert(h.requestTarget(2100, 120) && h.prepareReply() && h.replyContextRaw == 0x01020306);
  // The last ID can still be rebound while unsent, never after.
  h = head(0x123456fe);
  assert(h.requestTarget(1900, 100) && h.prepareReply() && h.replyContextRaw == 0x123456ff);
  assert(h.requestTarget(2000, 101) && h.prepareReply() && h.replyContextRaw == 0x123456ff);
  issue(h, 2, 110);
  assert(!h.requestTarget(2100, 120) && h.targetC100 == 2000);

  // Upper bytes survive; replace an unsent request without consuming an ID.
  h = head(0x01020304);
  assert(h.requestTarget(1900, 100) && h.requestTarget(2000, 101));
  assert(h.replyContextRaw == 0x01020304);
  issue(h, 3, 110);
  assert(h.replyContextRaw == 0x01020305 && h.targetC100 == 2000);
  uint8_t first[37]; memcpy(first, h.replyFrame, 37);
  assert(h.canRetryReply(true) && !h.canRetryReply(false));
  assert(h.prepareReply() && !memcmp(h.replyFrame, first, 37));
  assert(h.replyContextRaw == 0x01020305);
  // Repeated identical API requests also retain the existing ID/cache.
  assert(h.requestTarget(2000, 120) && h.canRetryReply(true));

  // Lost ACK: the old command may have applied. A different target gets a new
  // ID, even on a duplicate report; do not replay the old cached target.
  assert(h.requestTarget(2100, 130) && !h.canRetryReply(true));
  // ACK for the old packet records only its target and cannot consume the SET.
  assert(h.acknowledge(3, 131) && h.acceptedTargetC100 == 2000 && h.pendingExtended);
  issue(h, 4, 140);
  assert(h.replyContextRaw == 0x01020306 && h.targetC100 == 2100);
  assert(!h.acknowledge(3, 141));
  assert(h.acknowledge(4, 142) && !h.pendingExtended);
  assert(h.acceptedTargetC100 == 2100 && h.prepareReply());
  assert(h.replyContextRaw == 0x01020306);

  // A physical action cancels an unsent SET and uses one new local ID.
  assert(h.requestTarget(1600, 150));
  assert(h.localTarget(2300, 151) && !h.pendingExtended && !h.awaitingAck);
  assert(h.replyContextRaw == 0x01020307 && h.acceptedTargetC100 == 2300);
  assert(!h.acknowledge(4, 152) && h.replyContextRaw == 0x01020307);
  assert(h.localTarget(2300, 153) && h.replyContextRaw == 0x01020307);
  // A local echo of the already-issued remote target must not advance again.
  h = head(); assert(h.requestTarget(1900, 100)); issue(h, 2, 110);
  assert(h.localTarget(1900, 120) && h.replyContextRaw == 2 && !h.pendingExtended);

  // No guessed wrap/carry into opaque flags, no partial invalid request.
  h = head(0x123456fe);
  assert(h.requestTarget(1900, 100)); issue(h, 2, 110);
  assert(h.replyContextRaw == 0x123456ff);
  assert(!h.requestTarget(2000, 120) && h.targetC100 == 1900);
  assert(h.prepareReply() && h.replyContextRaw == 0x123456ff);
  assert(h.localTarget(2200, 130) && !h.enabled && !h.replyContextKnown);
  assert(h.targetC100 == 2200 && h.replyContextRaw == 0x123456ff);
  h = head(); assert(!h.requestTarget(399, 100) && !h.requestTarget(2801, 100));
  assert(h.replyContextRaw == 1 && !h.pendingExtended);

  // Crash after a transmission but before its ACK/checkpoint: candidate boot
  // policy discards the recovered context and ownership, retaining telemetry.
  Bridge::Registry registry;
  *registry.ensure(16) = head();
  auto *live = registry.find(16);
  assert(live->requestTarget(1900, 100)); issue(*live, 2, 110);
  auto saved = Bridge::captureState(registry, true);
  Bridge::Registry rebooted;
  assert(Bridge::restoreState(saved, rebooted));
  rebooted.find(16)->requireFreshContext();
  auto *boot = rebooted.find(16);
  assert(!boot->enabled && !boot->pendingExtended && !boot->replyContextKnown);
  assert(!boot->configurationKnown && boot->targetC100 == 1700);
  assert(!boot->requestTarget(2000, 200));
  // Even an older checkpoint from before TX is never used to assign another ID.
  *live = head(); saved = Bridge::captureState(registry, true);
  assert(Bridge::restoreState(saved, rebooted));
  boot = rebooted.find(16); boot->requireFreshContext();
  assert(!boot->enabled && !boot->requestTarget(2000, 200));
  // Handoff/re-arm also cannot combine an old accepted target with a used ID.
  h = head(); assert(h.requestTarget(1900, 100)); issue(h, 2, 110);
  h.stopControl();
  assert(h.targetC100 == 1700 && !h.replyContextKnown && !h.configurationKnown);

  // Deterministic interleavings over every possible starting byte: no two
  // different targets may be bound to an ID once it is on air (carried by a
  // reply or used by the head itself) within the same ownership.
  uint32_t random = 0x51c0ffee;
  for (unsigned initial = 0; initial < 256; ++initial) {
    h = head(0x12345600 | initial);
    int bindings[256]; for (auto &binding : bindings) binding = -1;
    bindings[initial] = 1700;
    for (unsigned step = 0; step < 128 && h.enabled; ++step) {
      random = random * 1664525u + 1013904223u;
      uint16_t target = 1600 + 100 * ((random >> 8) & 7);
      switch ((random >> 24) & 3) {
        case 0: if (h.requestTarget(target, step * 10)) h.prepareReply(); break; // as SET does
        case 1: h.localTarget(target, step * 10); break;
        case 2:
          if (h.pendingExtended) issue(h, step & 15, step * 10);
          break;
        case 3:
          if (h.replyLen) h.acknowledge(h.replyFrame[6], step * 10);
          break;
      }
      if (h.enabled && !h.contextUnsent) {
        assert((h.replyContextRaw & 0xffffff00) == 0x12345600);
        unsigned id = h.replyContextRaw & 255;
        assert(bindings[id] == -1 || bindings[id] == h.contextTargetC100);
        bindings[id] = h.contextTargetC100;
      }
      Bridge::Registry snapshot; *snapshot.ensure(16) = h;
      assert(Bridge::validSaved(Bridge::captureState(snapshot, false)));
    }
  }
  puts("manual updates: prospective trace, retries, lost ACK/replacement, local precedence, overflow and passive restart passed");
}
