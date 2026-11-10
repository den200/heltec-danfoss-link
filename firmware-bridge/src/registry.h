#pragma once
// Per-head state. Accepted targets, opaque CC context/configuration and ownership
// are persisted; sensor samples, sequences and pending commands stay in RAM. Tasks
// must hold regMutex (see main.cpp).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "danfoss_protocol.h"
namespace Bridge {

struct Head {
  uint8_t node = 0;
  bool present = false;        // slot in use
  bool enabled = false;        // bridge answers this head's reports
  bool targetKnown = false;    // a target was observed or explicitly set
  bool pendingExtended = false;// next reply must be the extended form (applies target)
  uint16_t targetC100 = 0;     // current desired target, C*100
  bool acceptedKnown = false;  // observed locally/from CC, or delivered by link ACK
  uint16_t acceptedTargetC100 = 0; // durable recovery target; never a pending SET
  uint32_t replyContextRaw = 0; // opaque four bytes, not a decoded mode/counter
  bool replyContextKnown = false;
  uint16_t contextTargetC100 = 0; // target bound to the currently issued manual ID
  bool manualContextUsed = false; // restart/handoff must relearn from the real CC
  bool contextUnsent = false;   // issued ID not yet in any built reply: may be rebound
  bool durableContextKnown = false;
  uint32_t durableContextRaw = 0;
  uint16_t durableContextTarget = 0;
  bool configurationKnown = false; // authenticated CC observation or validated recovery
  uint8_t configuration[10] = {};  // opaque extended-reply bytes at offsets 22..31
  bool hasLast = false;        // lastReport fields valid (duplicate detection)
  uint8_t lastRadioSeq = 0, lastAppSeq = 0;
  uint8_t lastFrame[57] = {}, lastFrameLen = 0;
  uint8_t replyFrame[37] = {}, replyLen = 0, replyAttempts = 0;
  bool awaitingAck = false;
  bool replyDeferred = false;
  uint32_t targetRevision = 0, replyRevision = 0;
  uint32_t lastSeenMs = 0, lastReplyMs = 0;
  uint32_t reports = 0, duplicates = 0, replies = 0, ccMsgs = 0, invalid = 0;
  uint32_t replyAcks = 0;
  uint32_t lastLocalMs = 0, pendingSinceMs = 0;
  bool localActionSeen = false;
  bool roomKnown = false, statusKnown = false;
  int16_t roomC100 = 0; // compensated room estimate; routine layout only
  // Other routine-report words, by correlation (data/HEAD-TEMPERATURES.md,
  // docs/HOME-ASSISTANT.md): front and valve-side sensors, valve opening in
  // tenths of a percent, and one unidentified word. Same freshness as roomC100.
  int16_t frontC100 = 0, valveC100 = 0;
  uint16_t valvePermille = 0, word46 = 0;
  uint16_t statusRaw = 0;
  uint32_t roomSeenMs = 0;
  uint32_t statusSeenMs = 0;
  int16_t rssi10 = 0;          // last report's signal, dBm*10

  void learnContext(uint32_t raw, uint16_t target) {
    replyContextRaw = raw;
    replyContextKnown = true;
    contextTargetC100 = target;
    manualContextUsed = false;
    contextUnsent = false;
    durableContextKnown = false;
  }

  void requireFreshContext() {
    stopControl();
    replyContextKnown = configurationKnown = false;
    contextTargetC100 = 0;
    manualContextUsed = false;
    contextUnsent = false;
  }

  // Genuine CC reply observed in SHADOW. An out-of-range setpoint must not leave
  // a half-learned context behind: it would make every later checkpoint invalid.
  void learnFromCc(uint32_t raw, uint16_t setpoint, const uint8_t *ccConfiguration) {
    if (setpoint < 400 || setpoint > 2800) { requireFreshContext(); return; }
    learnContext(raw, setpoint);
    targetC100 = acceptedTargetC100 = setpoint;
    targetKnown = acceptedKnown = true;
    if (ccConfiguration) {
      memcpy(configuration, ccConfiguration, sizeof configuration);
      configurationKnown = true;
    }
  }

  // Candidate manual-update rule, supported by a prospective CC observation.
  // Never carry into the other opaque bytes: wrap behavior is still unknown.
  // An ID no reply has carried yet is rebound instead of spending another, so
  // several SETs between two wakes cost one update. The changed binding is
  // still checkpointed before any reply carries it (replyDurable).
  bool bindManualTarget(uint16_t target) {
    if (!replyContextKnown) return false;
    if (contextTargetC100 == target) return true;
    if (!contextUnsent) {
      if ((replyContextRaw & 255) == 255) return false;
      ++replyContextRaw;
      contextUnsent = true;
    }
    contextTargetC100 = target;
    manualContextUsed = true;
    durableContextKnown = false;
    return true;
  }

  bool prepareReply() {
    return bindManualTarget(targetC100);
  }

  bool replyDurable() const {
    return replyContextKnown && durableContextKnown &&
           durableContextRaw == replyContextRaw && durableContextTarget == contextTargetC100;
  }

  bool canRetryReply(bool duplicate) const {
    return duplicate && replyLen && replyRevision == targetRevision;
  }

  bool localTarget(uint16_t target, uint32_t now) {
    if (target < 400 || target > 2800) return false;
    // In SHADOW only the real CC assigns IDs. Repeated reports of the same
    // local target while controlling must not allocate another ID.
    if (enabled && !bindManualTarget(target)) requireFreshContext();
    contextUnsent = false; // the head itself has now used this ID
    targetC100 = acceptedTargetC100 = target;
    targetKnown = acceptedKnown = true;
    pendingExtended = false;
    ++targetRevision;
    lastLocalMs = now;
    localActionSeen = true;
    replyLen = replyAttempts = 0;
    awaitingAck = false;
    replyDeferred = false;
    return true;
  }

  bool requestTarget(uint16_t target, uint32_t now) {
    if (target < 400 || target > 2800 || !replyContextKnown ||
        ((replyContextRaw & 255) == 255 && !contextUnsent && target != contextTargetC100)) return false;
    if (targetKnown && targetC100 == target) return true;
    targetC100 = target;
    targetKnown = pendingExtended = true;
    pendingSinceMs = now;
    ++targetRevision;
    return true;
  }

  void stopControl() {
    enabled = pendingExtended = awaitingAck = false;
    replyLen = replyAttempts = 0;
    replyDeferred = false;
    targetKnown = acceptedKnown;
    targetC100 = acceptedKnown ? acceptedTargetC100 : 0;
    ++targetRevision;
    if (manualContextUsed) {
      // An unacknowledged packet may still have applied. Do not re-arm using
      // an old accepted target paired with a newer issued ID.
      replyContextKnown = configurationKnown = false;
    }
  }

  bool observe(const uint8_t *frame, size_t n, uint32_t now) {
    auto kind = Danfoss::reportKind(frame, n);
    if (kind != Danfoss::ReportKind::Other && kind != Danfoss::ReportKind::Poll) {
      statusRaw = Danfoss::reportStatusRaw(frame, n);
      statusKnown = true;
      statusSeenMs = now;
      if (kind == Danfoss::ReportKind::Routine) {
        int16_t value = int16_t(uint16_t(frame[44]) << 8 | frame[45]);
        if (value >= -4000 && value <= 8500) {
          roomC100 = value; roomKnown = true; roomSeenMs = now;
          frontC100 = int16_t(uint16_t(frame[40]) << 8 | frame[41]);
          valveC100 = int16_t(uint16_t(frame[42]) << 8 | frame[43]);
          word46 = uint16_t(frame[46]) << 8 | frame[47];
          valvePermille = uint16_t(frame[48]) << 8 | frame[49];
        }
      }
    }
    bool duplicate = hasLast && lastFrameLen == n && now - lastSeenMs < 10000 &&
                     !memcmp(lastFrame, frame, n);
    reports++;
    if (duplicate) duplicates++;
    else {
      memcpy(lastFrame, frame, n);
      lastFrameLen = n;
      replyLen = replyAttempts = 0;
      awaitingAck = false;
      replyDeferred = false;
    }
    hasLast = true;
    lastRadioSeq = frame[6] & 15;
    lastAppSeq = frame[14] & 7;
    lastSeenMs = now;
    return duplicate;
  }

  bool acknowledge(uint8_t seq, uint32_t now) {
    if (!awaitingAck || !replyLen || (seq & 15) != (replyFrame[6] & 15) ||
        now - lastReplyMs > 1000) return false;
    awaitingAck = false;
    replyAcks++;
    acceptedTargetC100 = uint16_t(replyFrame[20]) << 8 | replyFrame[21];
    acceptedKnown = true;
    // Link delivery only, not proof the target was applied. A newer SET survives.
    if (replyRevision == targetRevision) pendingExtended = false;
    return true;
  }
};

class Registry {
 public:
  static constexpr size_t MAX_HEADS = 16;
  Head* find(uint8_t node) {
    for (size_t i = 0; i < count_; i++)
      if (heads_[i].node == node) return &heads_[i];
    return nullptr;
  }
  Head* ensure(uint8_t node) {
    // Only addressable head IDs; any other slot would fail checkpoint validation.
    if (node <= 1 || node > 232) return nullptr;
    if (Head* h = find(node)) return h;
    if (count_ >= MAX_HEADS) return nullptr;
    Head* h = &heads_[count_++];
    *h = Head();
    h->node = node;
    h->present = true;
    return h;
  }
  Head* begin() { return heads_; }
  Head* end() { return heads_ + count_; }
 private:
  Head heads_[MAX_HEADS];
  size_t count_ = 0;
};
}
