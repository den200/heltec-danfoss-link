#pragma once
// Portable storage codec: explicit byte layout, version/network binding and CRC.
// No radio/Arduino/flash dependencies. Pending commands are deliberately excluded.
#include "registry.h"

namespace Bridge {
struct SavedHead {
  uint8_t node = 0;
  bool enabled = false, known = false;
  uint16_t targetC100 = 0;
  uint32_t contextRaw = 0;
  uint16_t contextTargetC100 = 0;
  bool contextKnown = false, configurationKnown = false;
  uint8_t configuration[10] = {};
};
struct SavedState {
  uint32_t generation = 0;
  bool autostart = false;
  uint8_t count = 0;
  SavedHead heads[Registry::MAX_HEADS];
};
static constexpr size_t STATE_HEADER = 20, STATE_ENTRY = 20, STATE_V2_ENTRY = 18, STATE_V1_ENTRY = 6;
static constexpr size_t STATE_MAX_BYTES = STATE_HEADER + STATE_ENTRY * Registry::MAX_HEADS + 4;

inline uint32_t stateCrc(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffff;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
  }
  return ~c;
}
inline uint32_t stateWord(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline void statePut(uint8_t *p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
inline bool validSaved(const SavedState &s) {
  if (s.count > Registry::MAX_HEADS) return false;
  bool seen[233] = {};
  for (size_t i = 0; i < s.count; i++) {
    const SavedHead &h = s.heads[i];
    if (h.node <= 1 || h.node > 232 || seen[h.node]) return false;
    seen[h.node] = true;
    if (h.enabled && (!h.known || !h.contextKnown)) return false;
    if (h.contextKnown && (h.contextTargetC100 < 400 || h.contextTargetC100 > 2800)) return false;
    if (!h.contextKnown && h.contextTargetC100) return false;
    if (h.configurationKnown && !h.contextKnown) return false;
    if (h.known ? (h.targetC100 < 400 || h.targetC100 > 2800) : h.targetC100 != 0) return false;
  }
  return true;
}
inline bool newerGeneration(uint32_t a, uint32_t b) {
  return a != b && uint32_t(a - b) < 0x80000000u;
}
inline size_t encodeState(const SavedState &s, const uint8_t *home, const uint8_t *keyTag,
                          uint8_t *out, size_t capacity) {
  if (!validSaved(s)) return 0;
  size_t n = STATE_HEADER + s.count * STATE_ENTRY + 4;
  if (capacity < n) return 0;
  memset(out, 0, n);
  memcpy(out, "DFB1", 4); out[4] = 3; out[5] = s.count; out[6] = s.autostart;
  statePut(out + 8, s.generation);
  memcpy(out + 12, home, 4); memcpy(out + 16, keyTag, 4);
  for (size_t i = 0; i < s.count; i++) {
    const SavedHead &h = s.heads[i]; uint8_t *p = out + STATE_HEADER + i * STATE_ENTRY;
    p[0] = h.node; p[1] = (h.enabled ? 1 : 0) | (h.known ? 2 : 0) |
                        (h.contextKnown ? 4 : 0) | (h.configurationKnown ? 8 : 0);
    p[2] = h.targetC100 >> 8; p[3] = h.targetC100;
    statePut(p + 4, h.contextRaw);
    if (h.configurationKnown) memcpy(p + 8, h.configuration, 10);
    p[18] = h.contextTargetC100 >> 8; p[19] = h.contextTargetC100;
  }
  statePut(out + n - 4, stateCrc(out, n - 4));
  return n;
}
inline bool decodeState(const uint8_t *p, size_t n, const uint8_t *home,
                        const uint8_t *keyTag, SavedState &out) {
  if (n < STATE_HEADER + 4 || n > STATE_MAX_BYTES || memcmp(p, "DFB1", 4) ||
      (p[4] < 1 || p[4] > 3) || p[5] > Registry::MAX_HEADS || p[6] > 1 || p[7] ||
      n != STATE_HEADER + p[5] * (p[4] == 1 ? STATE_V1_ENTRY : p[4] == 2 ? STATE_V2_ENTRY : STATE_ENTRY) + 4 || memcmp(p + 12, home, 4) ||
      memcmp(p + 16, keyTag, 4) || stateWord(p + n - 4) != stateCrc(p, n - 4)) return false;
  SavedState s;
  bool legacy = p[4] == 1, oldContext = p[4] != 3;
  s.generation = stateWord(p + 8); s.count = p[5]; s.autostart = !oldContext && p[6];
  for (size_t i = 0; i < s.count; i++) {
    const uint8_t *entry = p + STATE_HEADER + i * (legacy ? STATE_V1_ENTRY : oldContext ? STATE_V2_ENTRY : STATE_ENTRY);
    if (entry[1] & (legacy ? ~3 : ~15)) return false;
    if (legacy && entry[5]) return false;
    if (legacy && (entry[1] & 1) && !(entry[1] & 2)) return false;
    SavedHead &h = s.heads[i]; h.node = entry[0];
    h.enabled = !oldContext && (entry[1] & 1); h.known = entry[1] & 2;
    h.targetC100 = uint16_t(entry[2]) << 8 | entry[3];
    if (legacy) {
      // V1 discarded upper context bytes and stored no configuration: migration
      // recovers observed targets, but cannot silently resume control.
      h.contextRaw = entry[4];
    } else {
      h.contextRaw = stateWord(entry + 4);
      h.contextKnown = !oldContext && (entry[1] & 4);
      h.configurationKnown = !oldContext && (entry[1] & 8);
      if (!oldContext) h.contextTargetC100 = uint16_t(entry[18]) << 8 | entry[19];
      if (h.configurationKnown) memcpy(h.configuration, entry + 8, 10);
      else if (!(entry[1] & 8)) for (size_t j = 8; j < 18; ++j) if (entry[j]) return false;
    }
  }
  if (!validSaved(s)) return false;
  out = s; // invalid input must not partially replace a usable state
  return true;
}
inline SavedState captureState(Registry &reg, bool autostart) {
  SavedState s; s.autostart = autostart;
  for (Head *h = reg.begin(); h != reg.end(); h++) {
    SavedHead &saved = s.heads[s.count++];
    saved.node = h->node; saved.known = h->acceptedKnown;
    saved.contextRaw = h->replyContextRaw; saved.contextKnown = h->replyContextKnown;
    saved.contextTargetC100 = saved.contextKnown ? h->contextTargetC100 : 0;
    saved.configurationKnown = h->configurationKnown;
    if (saved.configurationKnown) memcpy(saved.configuration, h->configuration, 10);
    saved.enabled = h->enabled && saved.known && saved.contextKnown;
    saved.targetC100 = saved.known ? h->acceptedTargetC100 : 0;
  }
  return s;
}
inline bool sameState(const SavedState &a, const SavedState &b) {
  if (a.autostart != b.autostart || a.count != b.count) return false;
  for (size_t i = 0; i < a.count; i++) {
    const SavedHead &x = a.heads[i], &y = b.heads[i];
    if (x.node != y.node || x.enabled != y.enabled || x.known != y.known ||
        x.targetC100 != y.targetC100 || x.contextRaw != y.contextRaw || x.contextTargetC100 != y.contextTargetC100 ||
        x.contextKnown != y.contextKnown || x.configurationKnown != y.configurationKnown ||
        (x.configurationKnown && memcmp(x.configuration, y.configuration, 10))) return false;
  }
  return true; // generation is storage metadata, not configuration
}
inline bool restoreState(const SavedState &s, Registry &reg) {
  if (!validSaved(s)) return false;
  Registry restored;
  for (size_t i = 0; i < s.count; i++) {
    const SavedHead &saved = s.heads[i]; Head *h = restored.ensure(saved.node);
    h->enabled = s.autostart && saved.enabled;
    h->acceptedKnown = h->targetKnown = saved.known;
    h->acceptedTargetC100 = h->targetC100 = saved.targetC100;
    h->replyContextRaw = saved.contextRaw; h->replyContextKnown = saved.contextKnown;
    h->contextTargetC100 = saved.contextTargetC100;
    if (saved.contextKnown) {
      h->targetC100 = saved.contextTargetC100;
      h->targetKnown = true;
      // A binding newer than the accepted target was issued by the bridge and may
      // have been sent: apply it with the extended form (CONTROL ON does the same
      // without autostart), and never re-arm on it after a handoff.
      bool issued = saved.contextTargetC100 != saved.targetC100;
      h->pendingExtended = h->enabled && issued;
      h->manualContextUsed = issued;
      h->durableContextKnown = true;
      h->durableContextRaw = saved.contextRaw;
      h->durableContextTarget = saved.contextTargetC100;
    }
    h->configurationKnown = saved.configurationKnown;
    memcpy(h->configuration, saved.configuration, 10);
    // Boot never replays a pending extended command or an old packet sequence.
  }
  reg = restored;
  return true;
}

// A write/readback confirms only the binding actually captured. Concurrent local
// changes or SETs must not inherit durability from an older snapshot.
inline void confirmCheckpoint(const SavedState &s, Registry &reg) {
  for (size_t i = 0; i < s.count; ++i) {
    const auto &saved = s.heads[i]; Head *h = reg.find(saved.node);
    if (h && saved.contextKnown && h->replyContextKnown &&
        h->replyContextRaw == saved.contextRaw && h->contextTargetC100 == saved.contextTargetC100) {
      h->durableContextKnown = true;
      h->durableContextRaw = saved.contextRaw;
      h->durableContextTarget = saved.contextTargetC100;
    }
  }
}
}
