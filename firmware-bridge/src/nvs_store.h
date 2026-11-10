#pragma once
#include <Preferences.h>
#include "persistent_state.h"

namespace Bridge {
// Write the inactive slot, read it back and validate; never delete the last good
// slot first. A torn/corrupt latest write falls back to the older valid record.
class NvsStore {
 public:
  bool begin(const uint8_t *home, const uint8_t *keyTag, const char *name = "danfoss") {
    memcpy(home_, home, 4); memcpy(keyTag_, keyTag, 4);
    ready_ = prefs_.begin(name, false);
    return ready_;
  }
  bool ready() const { return ready_; }
  bool hasSaved() const { return found_; }
  uint32_t generation() const { return found_ ? saved_.generation : 0; }
  const SavedState &saved() const { return saved_; }
  bool load(SavedState &out) {
    if (!ready_) return false;
    found_ = false;
    for (uint8_t slot = 0; slot < 2; slot++) {
      SavedState candidate;
      if (readSlot(slot, candidate) && (!found_ || newerGeneration(candidate.generation, saved_.generation))) {
        saved_ = candidate; slot_ = slot; found_ = true;
      }
    }
    if (found_) out = saved_;
    return found_;
  }
  bool save(const SavedState &state) {
    if (!ready_) return false;
    SavedState next = state; next.generation = found_ ? saved_.generation + 1 : 1;
    uint8_t bytes[STATE_MAX_BYTES];
    size_t n = encodeState(next, home_, keyTag_, bytes, sizeof bytes);
    uint8_t slot = found_ ? slot_ ^ 1 : 0;
    const char *name = slot ? "state1" : "state0";
    if (!n || prefs_.putBytes(name, bytes, n) != n) return false;
    SavedState checked;
    if (!readSlot(slot, checked) || checked.generation != next.generation || !sameState(checked, next)) return false;
    saved_ = checked; slot_ = slot; found_ = true;
    return true;
  }
 private:
  bool readSlot(uint8_t slot, SavedState &out) {
    const char *name = slot ? "state1" : "state0";
    size_t n = prefs_.getBytesLength(name);
    if (n < STATE_HEADER + 4 || n > STATE_MAX_BYTES) return false;
    uint8_t bytes[STATE_MAX_BYTES];
    return prefs_.getBytes(name, bytes, n) == n && decodeState(bytes, n, home_, keyTag_, out);
  }
  Preferences prefs_;
  uint8_t home_[4] = {}, keyTag_[4] = {}, slot_ = 0;
  bool ready_ = false, found_ = false;
  SavedState saved_;
};
}
