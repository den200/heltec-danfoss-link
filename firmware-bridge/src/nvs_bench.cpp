// Deliberate malformed-record tests in real NVS. Never uses the household namespace
// or radio. This tests fallback/readback, not electrical interruption of a write.
#include <Arduino.h>
#include "nvs_store.h"

static const uint8_t HOME[4] = {1, 2, 3, 4}, TAG[4] = {5, 6, 7, 8};
static const char *NS = "dfbench";

bool recovered(uint32_t generation, uint16_t target) {
  Bridge::NvsStore store;
  Bridge::SavedState state;
  return store.begin(HOME, TAG, NS) && store.load(state) &&
         state.generation == generation && state.heads[0].targetC100 == target;
}

bool bench() {
  Preferences raw;
  if (!raw.begin(NS, false) || !raw.clear()) return false;
  Bridge::SavedState oldState, newState;
  oldState.count = Bridge::Registry::MAX_HEADS;
  for (size_t i = 0; i < oldState.count; ++i) {
    oldState.heads[i].node = i + 2;
    oldState.heads[i].known = true;
    oldState.heads[i].targetC100 = 1700;
  }
  Bridge::NvsStore store;
  if (!store.begin(HOME, TAG, NS) || !store.save(oldState)) return false;
  newState = oldState;
  newState.heads[0].targetC100 = 1800;
  if (!store.save(newState) || !recovered(2, 1800)) return false;
  uint8_t bytes[Bridge::STATE_MAX_BYTES];
  newState.generation = 2;
  const size_t n = Bridge::encodeState(newState, HOME, TAG, bytes, sizeof bytes);
  unsigned cases = 0;
  // Slot 0 holds generation 1. Replace slot 1 with every nonempty truncation.
  for (size_t cut = 1; cut < n; ++cut) {
    if (raw.putBytes("state1", bytes, cut) != cut || !recovered(1, 1700)) return false;
    ++cases;
  }
  if (!raw.remove("state1") || !recovered(1, 1700)) return false;
  ++cases;
  // Every single-byte corruption, including header, payload and stored CRC.
  for (size_t pos = 0; pos < n; ++pos) {
    bytes[pos] ^= 1;
    bool ok = raw.putBytes("state1", bytes, n) == n && recovered(1, 1700);
    bytes[pos] ^= 1;
    if (!ok) return false;
    ++cases;
  }
  if (raw.putBytes("state1", bytes, n) != n || !recovered(2, 1800)) return false;
  // Both invalid must fail closed, never return a partially decoded state.
  bytes[0] ^= 1;
  if (raw.putBytes("state0", bytes, n) != n || raw.putBytes("state1", bytes, n) != n) return false;
  Bridge::NvsStore broken;
  Bridge::SavedState decoded;
  if (!broken.begin(HOME, TAG, NS) || broken.load(decoded)) return false;
  ++cases;
  if (!raw.clear()) return false;
  Serial.printf("{\"t\":\"bench_result\",\"passed\":true,\"fault_cases\":%u,\"record_bytes\":%u,\"namespace\":\"dfbench\",\"power_interruption_tested\":false}\n", cases, unsigned(n));
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(3000); // let the sole capture service reconnect after flashing
  Serial.println("{\"t\":\"boot\",\"fw\":\"nvs-bench-1\",\"radio\":false}");
  if (!bench()) Serial.println("{\"t\":\"bench_result\",\"passed\":false}");
  Serial.println("{\"t\":\"bench_done\"}");
}
void loop() { delay(1000); }
