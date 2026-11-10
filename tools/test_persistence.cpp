#include "../firmware-bridge/src/nvs_store.h"
#include "../firmware-bridge/src/danfoss_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <fstream>
#include <iterator>
namespace FakeNvs {
std::map<std::string, std::vector<unsigned char>> values;
Fault fault = None;
bool openOk = true;
}
static const uint8_t HOME[4] = {0xff, 0xd2, 0xbb, 0x2d}, TAG[4] = {1, 2, 3, 4};
int main(int argc, char **argv) {
  if (argc == 2) {
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> old((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    assert(old.size() >= 24 && old[4] == 1);
    Bridge::SavedState migrated, roundtrip;
    assert(Bridge::decodeState(old.data(), old.size(), HOME, old.data() + 16, migrated));
    assert(!migrated.autostart);
    for (size_t i = 0; i < migrated.count; ++i)
      assert(!migrated.heads[i].enabled && !migrated.heads[i].contextKnown && !migrated.heads[i].configurationKnown);
    uint8_t current[Bridge::STATE_MAX_BYTES];
    size_t size = Bridge::encodeState(migrated, HOME, old.data() + 16, current, sizeof current);
    assert(size && current[4] == 3 && Bridge::decodeState(current, size, HOME, old.data() + 16, roundtrip));
    assert(Bridge::sameState(migrated, roundtrip));
    fprintf(stderr, "Production V1 generation %lu, %u heads: passive V2 migration/roundtrip passed\n",
            (unsigned long)migrated.generation, migrated.count);
  }
  assert(Bridge::stateCrc(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0xcbf43926);
  Bridge::Registry reg;
  Bridge::Head *h = reg.ensure(15);
  h->enabled = h->targetKnown = h->acceptedKnown = true;
  h->learnContext(0x00000101, 1700);
  h->replyContextKnown = h->configurationKnown = true;
  const uint8_t ccConfig[] = {0x06,0xa4,2,0x58,0x0a,0xf0,0,0,0x0a,0xf0};
  memcpy(h->configuration, ccConfig, sizeof ccConfig);
  h->acceptedTargetC100 = 1700;
  h->targetC100 = 1800; h->pendingExtended = true; // never replay this unacked SET
  Bridge::SavedState state = Bridge::captureState(reg, false), decoded;
  uint8_t bytes[Bridge::STATE_MAX_BYTES];
  size_t n = Bridge::encodeState(state, HOME, TAG, bytes, sizeof bytes);
  assert(n && Bridge::decodeState(bytes, n, HOME, TAG, decoded));
  Bridge::Registry restored;
  assert(Bridge::restoreState(decoded, restored));
  assert(restored.find(15)->targetC100 == 1700 && !restored.find(15)->enabled);
  assert(!restored.find(15)->pendingExtended && !restored.find(15)->hasLast);
  assert(restored.find(15)->replyContextRaw == 0x00000101 && restored.find(15)->replyContextKnown);
  assert(restored.find(15)->configurationKnown && !memcmp(restored.find(15)->configuration, ccConfig, sizeof ccConfig));
  state.autostart = true;
  assert(Bridge::restoreState(state, restored) && restored.find(15)->enabled);
  // Corrupt/truncated/wrong-network records never partially overwrite good output.
  decoded.generation = 123;
  bytes[8] ^= 1;
  assert(!Bridge::decodeState(bytes, n, HOME, TAG, decoded) && decoded.generation == 123);
  n = Bridge::encodeState(state, HOME, TAG, bytes, sizeof bytes);
  assert(!Bridge::decodeState(bytes, n - 1, HOME, TAG, decoded));
  for (size_t truncated = 0; truncated < n; truncated++)
    assert(!Bridge::decodeState(bytes, truncated, HOME, TAG, decoded));
  uint8_t other[4] = {};
  assert(!Bridge::decodeState(bytes, n, other, TAG, decoded));
  assert(!Bridge::decodeState(bytes, n, HOME, other, decoded));
  bytes[4] = 4;
  Bridge::statePut(bytes + n - 4, Bridge::stateCrc(bytes, n - 4));
  assert(!Bridge::decodeState(bytes, n, HOME, TAG, decoded));
  // Legacy V1 carries only a truncated mode byte: recover targets, never arm.
  uint8_t legacy[30] = {};
  memcpy(legacy, "DFB1", 4); legacy[4] = 1; legacy[5] = 1; legacy[6] = 1;
  Bridge::statePut(legacy + 8, 34); memcpy(legacy + 12, HOME, 4); memcpy(legacy + 16, TAG, 4);
  legacy[20] = 15; legacy[21] = 3; legacy[22] = 0x06; legacy[23] = 0xa4; legacy[24] = 0x2a;
  Bridge::statePut(legacy + 26, Bridge::stateCrc(legacy, 26));
  assert(Bridge::decodeState(legacy, sizeof legacy, HOME, TAG, decoded));
  assert(decoded.generation == 34 && !decoded.autostart && !decoded.heads[0].enabled);
  assert(decoded.heads[0].targetC100 == 1700 && !decoded.heads[0].contextKnown && !decoded.heads[0].configurationKnown);
  assert(Bridge::restoreState(decoded, restored) && !restored.find(15)->enabled);
  // Corrupt every byte of a V2 record, including the new context/template fields.
  n = Bridge::encodeState(state, HOME, TAG, bytes, sizeof bytes);
  for (size_t pos = 0; pos < n; ++pos) {
    bytes[pos] ^= 1;
    assert(!Bridge::decodeState(bytes, n, HOME, TAG, decoded));
    bytes[pos] ^= 1;
  }
  // A torn first V2 save must fall back to V1 in passive mode; never resurrect
  // the V1 autostart bit with a guessed/truncated context or missing template.
  FakeNvs::values["migration/state0"] = std::vector<unsigned char>(legacy, legacy + sizeof legacy);
  Bridge::NvsStore migration;
  assert(migration.begin(HOME, TAG, "migration") && migration.load(decoded));
  assert(decoded.generation == 34 && !decoded.autostart && !decoded.heads[0].enabled);
  FakeNvs::fault = FakeNvs::TornWrite;
  assert(!migration.save(state));
  Bridge::NvsStore migrationFallback;
  assert(migrationFallback.begin(HOME, TAG, "migration") && migrationFallback.load(decoded));
  assert(decoded.generation == 34 && !decoded.autostart && !decoded.heads[0].contextKnown);
  FakeNvs::fault = FakeNvs::None;
  assert(migrationFallback.save(state));
  Bridge::NvsStore migrationReboot;
  assert(migrationReboot.begin(HOME, TAG, "migration") && migrationReboot.load(decoded));
  assert(decoded.generation == 35 && decoded.autostart && decoded.heads[0].contextRaw == 0x101);
  assert(decoded.heads[0].configurationKnown && !memcmp(decoded.heads[0].configuration, ccConfig, 10));
  FakeNvs::values["migration/state1"].back() ^= 1;
  Bridge::NvsStore migrationCorrupt;
  assert(migrationCorrupt.begin(HOME, TAG, "migration") && migrationCorrupt.load(decoded));
  assert(decoded.generation == 34 && !decoded.autostart && !decoded.heads[0].enabled);
  Bridge::SavedState full;
  for (size_t i = 0; i < Bridge::Registry::MAX_HEADS; i++) full.heads[full.count++].node = i + 2;
  size_t maximum = Bridge::encodeState(full, HOME, TAG, bytes, sizeof bytes);
  assert(maximum == Bridge::STATE_MAX_BYTES && Bridge::decodeState(bytes, maximum, HOME, TAG, decoded));
  assert(!Bridge::encodeState(full, HOME, TAG, bytes, sizeof bytes - 1));
  Bridge::SavedState bad = state;
  bad.count = 2; bad.heads[1] = bad.heads[0];
  assert(!Bridge::encodeState(bad, HOME, TAG, bytes, sizeof bytes));
  bad = state; bad.heads[0].targetC100 = 390;
  assert(!Bridge::encodeState(bad, HOME, TAG, bytes, sizeof bytes));
  // Unknown/unconfirmed targets cannot be automatically armed after a reboot.
  h->acceptedKnown = false;
  auto unknown = Bridge::captureState(reg, true);
  assert(!unknown.heads[0].known && !unknown.heads[0].enabled && !unknown.heads[0].targetC100);
  h->acceptedKnown = true;
  // Slot fallback: torn new write and failed write preserve old 17 C checkpoint.
  Bridge::NvsStore store;
  assert(store.begin(HOME, TAG) && !store.load(decoded));
  assert(store.save(state) && store.generation() == 1);
  state.heads[0].targetC100 = 1900;
  FakeNvs::fault = FakeNvs::TornWrite;
  assert(!store.save(state));
  Bridge::NvsStore afterPowerLoss;
  assert(afterPowerLoss.begin(HOME, TAG) && afterPowerLoss.load(decoded));
  assert(decoded.generation == 1 && decoded.heads[0].targetC100 == 1700);
  FakeNvs::fault = FakeNvs::WriteFailure;
  assert(!afterPowerLoss.save(state));
  FakeNvs::fault = FakeNvs::None;
  assert(afterPowerLoss.save(state));
  Bridge::NvsStore nextBoot;
  assert(nextBoot.begin(HOME, TAG) && nextBoot.load(decoded));
  assert(decoded.generation == 2 && decoded.heads[0].targetC100 == 1900);
  FakeNvs::values["danfoss/state1"][10] ^= 1;
  Bridge::NvsStore corruptLatest;
  assert(corruptLatest.begin(HOME, TAG) && corruptLatest.load(decoded));
  assert(decoded.generation == 1 && decoded.heads[0].targetC100 == 1700);
  Bridge::NvsStore wrongNetwork;
  assert(wrongNetwork.begin(other, TAG) && !wrongNetwork.load(decoded));
  assert(Bridge::newerGeneration(0, 0xffffffff) && !Bridge::newerGeneration(0xffffffff, 0));
  // Later packet counters and transient pending commands do not change checkpoints.
  auto before = Bridge::captureState(reg, true);
  h->reports++; h->lastSeenMs = 1234; h->targetC100 = 2200;
  assert(Bridge::sameState(before, Bridge::captureState(reg, true)));
  // Even an old-but-valid ACK records its delivered target, not a newer pending SET.
  uint8_t key[16] = {};
  const uint8_t configuration[] = {0x06,0xa4,2,0x58,0x0a,0xf0,0,0,0x0a,0xf0};
  h->replyLen = Danfoss::buildExtendedReply(HOME, 15, 3, 1, 3, 1800, key, h->replyFrame, configuration);
  h->replyRevision = 1; h->targetRevision = 2; h->awaitingAck = true; h->lastReplyMs = 100;
  assert(h->acknowledge(3, 110) && h->acceptedTargetC100 == 1800 && h->pendingExtended);
  assert(Bridge::captureState(reg, true).heads[0].targetC100 == 1800);
  // A local action after a queued request wins, including after app sequence wrap.
  h->learnContext(3, 1800);
  assert(h->requestTarget(2200, 1000));
  assert(h->pendingExtended && h->targetC100 == 2200);
  assert(h->localTarget(1750, 1010));
  assert(!h->pendingExtended && h->targetC100 == 1750 && h->acceptedTargetC100 == 1750);
  assert(!h->awaitingAck && !h->replyLen && h->lastLocalMs == 1010);
  assert(!h->acknowledge(3, 1011) && h->acceptedTargetC100 == 1750);
  assert(!h->localTarget(390, 1020) && h->targetC100 == 1750);
  assert(h->requestTarget(1800, 1030));
  assert(h->pendingExtended && h->targetC100 == 1800 && h->acceptedTargetC100 == 1750);
  // Relinquishing ownership cancels RAM requests/caches and permits CC learning.
  h->enabled = h->awaitingAck = true;
  h->replyLen = 37;
  h->stopControl();
  assert(!h->enabled && !h->pendingExtended && !h->awaitingAck && !h->replyLen);
  assert(h->targetKnown && h->targetC100 == 1750);
  assert(!h->acknowledge(3, 1040) && h->acceptedTargetC100 == 1750);
  Bridge::Head unconfirmed;
  unconfirmed.enabled = true;
  unconfirmed.requestTarget(1900, 2000);
  unconfirmed.stopControl();
  assert(!unconfirmed.targetKnown && !unconfirmed.targetC100 && !unconfirmed.pendingExtended);
  // Bench namespace must never replace or clear production records.
  const auto production = FakeNvs::values;
  Bridge::NvsStore isolated;
  assert(isolated.begin(HOME, TAG, "dfbench") && !isolated.load(decoded));
  assert(isolated.save(state));
  for (const auto &entry : production) assert(FakeNvs::values[entry.first] == entry.second);
  FakeNvs::openOk = false;
  Bridge::NvsStore unavailable;
  assert(!unavailable.begin(HOME, TAG) && !unavailable.save(state));
  // Out-of-range CC observations must never make the whole checkpoint unencodable.
  {
    Bridge::Registry observed;
    assert(!observed.ensure(0) && !observed.ensure(1) && !observed.ensure(233) && !observed.ensure(255));
    const uint8_t ccConfiguration[10] = {0x07, 0x08, 1, 2, 3, 4, 5, 6, 7, 8};
    Bridge::Head *good = observed.ensure(8), *odd = observed.ensure(12);
    good->learnFromCc(0x00000101, 1850, ccConfiguration);
    assert(good->replyContextKnown && good->targetKnown && good->acceptedTargetC100 == 1850 &&
           good->configurationKnown && !memcmp(good->configuration, ccConfiguration, 10));
    odd->learnFromCc(0x00000101, 1800, nullptr);
    assert(odd->replyContextKnown && !odd->configurationKnown);
    for (uint16_t bad : {uint16_t(0), uint16_t(399), uint16_t(2801), uint16_t(0xffff)}) {
      odd->learnFromCc(0x00000102, bad, ccConfiguration);
      assert(!odd->replyContextKnown && !odd->configurationKnown && !odd->contextTargetC100);
      assert(odd->acceptedKnown && odd->acceptedTargetC100 == 1800); // last good target kept
      uint8_t encoded[Bridge::STATE_MAX_BYTES];
      Bridge::SavedState snapshot = Bridge::captureState(observed, true);
      assert(Bridge::encodeState(snapshot, HOME, TAG, encoded, sizeof encoded));
    }
  }
  puts("persistence: recovery, pending-command exclusion, network binding and torn-write fallback passed");
}
