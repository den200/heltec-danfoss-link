#include "../firmware-bridge/src/persistent_state.h"
#include <assert.h>
#include <stdio.h>

int main() {
  Bridge::Registry reg;
  auto *h = reg.ensure(16);
  h->enabled = h->acceptedKnown = h->targetKnown = true;
  h->acceptedTargetC100 = h->targetC100 = 1700;
  h->learnContext(1, 1700);
  assert(!h->replyDurable());
  auto checkpoint = Bridge::captureState(reg, true);
  Bridge::confirmCheckpoint(checkpoint, reg);
  assert(h->replyDurable());
  // An unsent request is not replayed after restart.
  assert(h->requestTarget(1900, 100));
  auto unsent = Bridge::captureState(reg, true);
  assert(unsent.heads[0].contextTargetC100 == 1700);
  Bridge::Registry reboot;
  assert(Bridge::restoreState(unsent, reboot));
  assert(reboot.find(16)->enabled && reboot.find(16)->targetC100 == 1700);
  // Allocating an ID blocks TX until this exact binding is checkpointed.
  assert(h->prepareReply() && h->replyContextRaw == 2 && !h->replyDurable());
  auto reserved = Bridge::captureState(reg, true);
  assert(reserved.heads[0].contextTargetC100 == 1900 && reserved.heads[0].targetC100 == 1700);
  Bridge::confirmCheckpoint(checkpoint, reg); // stale completion cannot authorize TX
  assert(!h->replyDurable());
  Bridge::confirmCheckpoint(reserved, reg);
  assert(h->replyDurable());
  // Crash just before TX and crash after TX/lost ACK have the same durable
  // intent. Both resend ID2/19C; neither allocates ID2 for another target.
  for (int crash = 0; crash < 2; ++crash) {
    assert(Bridge::restoreState(reserved, reboot));
    auto *r = reboot.find(16);
    assert(r->enabled && r->pendingExtended && r->prepareReply());
    assert(r->replyDurable() && r->replyContextRaw == 2 && r->targetC100 == 1900);
    assert(r->requestTarget(2200, 200) && r->prepareReply());
    assert(r->replyContextRaw == 3 && !r->replyDurable());
  }
  // Without autostart the head comes back disabled, but the issued binding is
  // still known as issued: a handoff cannot re-arm on it.
  {
    auto manual = reserved; manual.autostart = false;
    assert(Bridge::restoreState(manual, reboot));
    auto *r = reboot.find(16);
    assert(!r->enabled && !r->pendingExtended && r->manualContextUsed && r->targetC100 == 1900);
    r->stopControl();
    assert(!r->replyContextKnown && r->targetC100 == 1700);
    // A restored ID may already be on air: a new target spends the next one.
    assert(Bridge::restoreState(reserved, reboot));
    r = reboot.find(16);
    assert(!r->contextUnsent && r->requestTarget(2000, 200) && r->prepareReply() && r->replyContextRaw == 3);
  }
  // Local action while an older save is finishing cannot inherit its durability.
  // ID2 never left the bridge, so the head's own action takes it.
  assert(h->localTarget(2200, 300));
  Bridge::confirmCheckpoint(reserved, reg);
  assert(h->replyContextRaw == 2 && h->contextTargetC100 == 2200 && !h->replyDurable());
  auto local = Bridge::captureState(reg, true);
  Bridge::confirmCheckpoint(local, reg);
  assert(h->replyDurable());
  // V3 codec includes the intent pair; corruption still fails closed.
  uint8_t bytes[Bridge::STATE_MAX_BYTES], home[4] = {1,2,3,4}, tag[4] = {5,6,7,8};
  auto n = Bridge::encodeState(reserved, home, tag, bytes, sizeof bytes);
  Bridge::SavedState decoded;
  assert(n && bytes[4] == 3 && Bridge::decodeState(bytes,n,home,tag,decoded));
  assert(decoded.heads[0].contextTargetC100 == 1900);
  bytes[Bridge::STATE_HEADER + 19] ^= 1;
  assert(!Bridge::decodeState(bytes,n,home,tag,decoded));
  // Migrate actual V2 layout passively: old target/context pairs were ambiguous.
  n = Bridge::encodeState(reserved, home, tag, bytes, sizeof bytes);
  bytes[4] = 2; n -= 2;
  Bridge::statePut(bytes+n-4, Bridge::stateCrc(bytes,n-4));
  assert(Bridge::decodeState(bytes,n,home,tag,decoded));
  assert(!decoded.autostart && !decoded.heads[0].enabled && !decoded.heads[0].contextKnown);
  puts("durable updates: pre-TX reservation, lost-ACK restart, replacement/local races, V3 CRC and passive V2 migration passed");
}
