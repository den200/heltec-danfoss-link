// danfoss-bridge firmware (version: BridgeEntities::FIRMWARE): spare-only manual-update candidate.
//
// Takes over the Danfoss Link CC's steady-state role: continuously receives
// authenticated head reports on 868.40 MHz FSK and, for enabled heads, sends the
// link ACK plus an authenticated reply carrying the current target, so heads
// never lose controller contact (connection/alarm icon).
//
// Fresh installs and old storage formats boot in SHADOW. Explicit AUTOSTART ON
// recovers V3 ownership and a target/context binding checkpointed before TX.
//
// Serial line commands (115200):
//   SHADOW                  disable all control replies (default at boot)
//   CONTROL <node> ON|OFF   answer this head's reports (requires a known target)
//   SET <node> <temp>       target in C (21.5) or C*100 (2150); next reply is extended
//   STATUS                  dump the registry as JSON
//   AUTOSTART ON|OFF        opt in/out of saved control on reboot (default OFF)
//   SAVE                    queue a checkpoint; wait for the saved event
//   STOP                    alias for SHADOW
// Events are JSON lines on USB serial, as in the listening-post prototype, and
// are mirrored to MQTT (danfoss/<home-id>/bridge/event, radio frames under
// danfoss/<home-id>/debug/frame) so the bridge runs without a serial host.

#include <Arduino.h>
#include <ctype.h>
#include <stdarg.h>
#include <algorithm>
#include <atomic>
#include <RadioLib.h>
#include <SSD1306Wire.h>

#include "danfoss_protocol.h"
#include "danfoss_key.h"
#include "registry.h"
#include "nvs_store.h"
#include "network.h"

// Heltec V3 pins
#define P_NSS 8
#define P_DIO1 14
#define P_RST 12
#define P_BUSY 13
#define P_SCK 9
#define P_MISO 11
#define P_MOSI 10
#define P_VEXT 36
#define P_OLED_RST 21
#define TCXO_V 1.8

// Danfoss network parameters: the household's Home ID comes from household.h
static const uint8_t HOME[4] = {Household::homeByte(0), Household::homeByte(1),
                                Household::homeByte(2), Household::homeByte(3)};
static const float RF_FREQ = 868.40, RF_BR = 40.0, RF_DEV = 20.0, RF_RXBW = 117.3;
// Prototype used 0 dBm at bench range, 10 dBm for real-room coverage, 14 dBm since
// 0.15.6 for the far head. EU SRD limit at 868.4 MHz is 25 mW ERP (~14 dBm) — do
// not raise above that.
static const int8_t RF_POWER = 14;
// RX window: 4-byte sync consumes the Home ID, receive the rest of a max-size
// (57 B) frame. Shorter frames complete on trailing noise; declared length decides.
static const uint8_t RX_SYNC[4] = {uint8_t(~HOME[0]), uint8_t(~HOME[1]), uint8_t(~HOME[2]),
                                   uint8_t(~HOME[3])};  // inverted Home ID
static const size_t RX_WINDOW = 53;
#define HOUSEHOLD_NODE_(n, room) n,
static const uint8_t KNOWN_NODES[] = {HOUSEHOLD_HEADS(HOUSEHOLD_NODE_)};

SPIClass spi(FSPI);
SX1262 radio = new Module(P_NSS, P_DIO1, P_RST, P_BUSY, spi);
SSD1306Wire oled(0x3c, 17, 18);

Bridge::Registry reg;
SemaphoreHandle_t regMutex;
SemaphoreHandle_t rxSem;
uint8_t txRadioSeq = 0;  // own radio-sequence counter for replies
Bridge::NvsStore stateStore;
bool autostart = false, persistenceDirty = false, saveRequested = false;
std::atomic<uint32_t> radioActivityMs{0}, eventQueueDrops{0};
std::atomic<bool> radioBusy{false};
std::atomic<uint32_t> rxSignalUs{0}, maxRxTaskDelayUs{0};
uint32_t lastSaveUs = 0, maxSaveUs = 0, saveFailures = 0;
Bridge::SavedState observedConfig;
bool haveObservedConfig = false;
uint32_t configChangedMs = 0, lastSaveAttemptMs = 0;
uint32_t checkpointRetryAtMs = 0;
BridgeNet::Diag diag; // loop task only; copied into network snapshots

// One JSON event line to the serial console and, when connected, to MQTT.
// Loop task only. The latest error is kept for the bridge state topic.
void emit(const char *fmt, ...) {
  char line[256];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  if (n < 0) return;
  Serial.println(line);
  if (!strncmp(line, "{\"t\":\"err\"", 11) && size_t(n) < sizeof diag.lastError) {
    memcpy(diag.lastError, line, n + 1);
    diag.hasError = true;
    diag.lastErrorMs = millis();
  }
  BridgeNet::event(line);
}

// Called from the console/loop only. Flash writes never run in the radio task.
bool saveStateNow() {
  if (radioBusy.load()) return false; // finish the immediate radio ACK first
  if (!stateStore.ready()) {
    emit("{\"t\":\"err\",\"where\":\"storage\",\"why\":\"NVS unavailable\"}");
    return false;
  }
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
  Bridge::SavedState state = Bridge::captureState(reg, autostart);
  xSemaphoreGive(regMutex);
  bool unchanged = stateStore.hasSaved() && Bridge::sameState(state, stateStore.saved());
  uint32_t saveStartMs = millis(), durationUs = 0;
  if (!unchanged) {
    uint32_t startedUs = micros();
    bool ok = stateStore.save(state);
    durationUs = micros() - startedUs;
    lastSaveUs = durationUs;
    if (durationUs > maxSaveUs) maxSaveUs = durationUs;
    if (!ok) {
      ++saveFailures;
      emit("{\"t\":\"err\",\"where\":\"storage\",\"why\":\"checkpoint write/readback failed\",\"duration_us\":%lu}", (unsigned long)durationUs);
      return false;
    }
  }
  persistenceDirty = saveRequested = false;
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    Bridge::confirmCheckpoint(state, reg);
    xSemaphoreGive(regMutex);
  }
  observedConfig = state; haveObservedConfig = true;
  emit("{\"t\":\"saved\",\"generation\":%lu,\"autostart\":%s,\"unchanged\":%s,\"start_ms\":%lu,\"duration_us\":%lu}",
                (unsigned long)stateStore.generation(), autostart ? "true" : "false", unchanged ? "true" : "false",
                (unsigned long)saveStartMs, (unsigned long)durationUs);
  return true;
}

void persistenceTick() {
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (!stateStore.ready() || radioBusy.load() || now - lastPoll < 5) return;
  lastPoll = now;
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
  Bridge::SavedState state = Bridge::captureState(reg, autostart);
  bool urgent = false;
  for (auto *h = reg.begin(); h != reg.end(); ++h)
    urgent |= h->enabled && h->replyContextKnown && !h->replyDurable();
  if (stateStore.hasSaved() && Bridge::sameState(state, stateStore.saved()))
    Bridge::confirmCheckpoint(stateStore.saved(), reg);
  xSemaphoreGive(regMutex);
  if (stateStore.hasSaved() && Bridge::sameState(state, stateStore.saved())) {
    persistenceDirty = false;
    if (saveRequested) saveStateNow();
    return;
  }
  if (!haveObservedConfig || !Bridge::sameState(state, observedConfig)) {
    observedConfig = state; haveObservedConfig = true; configChangedMs = now;
  }
  persistenceDirty = true;
  if (urgent) {
    if (!checkpointRetryAtMs || int32_t(now - checkpointRetryAtMs) >= 0) {
      // Back off only after a real write failure; a busy radio or a missed mutex
      // retries on the next tick, inside the 100 ms deferred-reply window.
      uint32_t failuresBefore = saveFailures;
      bool saved = saveStateNow();
      checkpointRetryAtMs = saved || saveFailures == failuresBefore ? 0 : now + 1000;
    }
    return; // write-ahead binding before radio TX; failed storage never releases it
  }
  // Coalesce accepted-target changes. Quiet-time gating reduces interference;
  // flash latency under RF load still requires hardware testing.
  if ((!saveRequested && now - configChangedMs < 5000) || now - radioActivityMs.load(std::memory_order_relaxed) < 1000 ||
      now - lastSaveAttemptMs < 5000) return;
  lastSaveAttemptMs = now;
  saveStateNow();
}

struct Event {
  uint8_t type;     // 1 report, 2 cc, 3 dup, 4 link-ack, 5 invalid, 6 tx, 7 other
  uint8_t node;     // source node
  uint8_t op;
  uint8_t len;      // declared frame length (0 if undecodable)
  int16_t rssi10;
  uint32_t procUs;  // tx events: processing before first transmission
  uint32_t queuedMs;
  uint8_t frame[57];
  int16_t ackStatus, responseStatus;
};
QueueHandle_t eventQueue;

void IRAM_ATTR onRx() {
  rxSignalUs.store(micros(), std::memory_order_relaxed);
  BaseType_t w = pdFALSE;
  xSemaphoreGiveFromISR(rxSem, &w);
  if (w) portYIELD_FROM_ISR();
}

void pushEvent(uint8_t type, uint8_t node, uint8_t op, uint8_t len, float rssi,
               uint32_t procUs, const uint8_t *frame, int ackStatus = 0, int responseStatus = 0) {
  Event e = {};
  e.ackStatus = ackStatus; e.responseStatus = responseStatus;
  e.type = type; e.node = node; e.op = op; e.len = len;
  e.rssi10 = int16_t(rssi * 10); e.procUs = procUs;
  e.queuedMs = millis();
  memset(e.frame, 0, sizeof e.frame);
  if (frame) memcpy(e.frame, frame, len <= sizeof e.frame ? len : sizeof e.frame);
  if (xQueueSend(eventQueue, &e, 0) != pdTRUE)
    eventQueueDrops.fetch_add(1, std::memory_order_relaxed);
}

void rxListen(size_t window = RX_WINDOW) {
  radio.standby();
  radio.setSyncWord((uint8_t *)RX_SYNC, sizeof RX_SYNC);
  radio.fixedPacketLengthMode(window);
  radio.setPacketReceivedAction(onRx);
  radio.startReceive();
}

int sendFrame(const uint8_t *f, size_t n) {
  radio.standby();
  radio.clearPacketReceivedAction();
  uint8_t sync[] = {0xaa, 0x0f};
  int st = radio.setSyncWord(sync, 2);
  if (st == 0) st = radio.fixedPacketLengthMode(n);
  uint8_t wire[64];
  for (size_t i = 0; i < n; i++) wire[i] = f[i] ^ 255;
  return st == 0 ? radio.transmit(wire, n) : st;
}

// Reply to an authenticated head report: link ACK, then the application reply.
// Extended form when a new target must be applied, short routine form otherwise.
// Runs in the radio task; no network, flash or logging calls here.
void answerReport(uint8_t node, const uint8_t *frame, bool duplicate, uint32_t receivedUs,
                  bool linkAlreadyAcked = false) {
  uint8_t ack[10], resp[37];
  size_t respLen = 0;
  uint32_t revision = 0;
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
  Bridge::Head *h = reg.find(node);
  if (h && h->enabled && h->targetKnown && h->replyContextKnown) {
    revision = h->targetRevision;
    if (h->canRetryReply(duplicate)) {
      // Repeat the same reply, including its radio sequence, at most three times.
      if (h->replyAttempts < 3) {
        respLen = h->replyLen;
        memcpy(resp, h->replyFrame, respLen);
      }
    } else {
      if (!h->prepareReply()) {
        h->requireFreshContext();
        xSemaphoreGive(regMutex);
        return;
      }
      if (h->replyDurable()) {
      if (h->pendingExtended && h->configurationKnown) {
        // Candidate manual override: one new ID, both target words changed,
        // opaque configuration tail preserved. Retries retain that same ID.
        uint8_t requestedConfiguration[10];
        memcpy(requestedConfiguration, h->configuration, sizeof requestedConfiguration);
        requestedConfiguration[0] = h->targetC100 >> 8;
        requestedConfiguration[1] = h->targetC100;
        respLen = Danfoss::buildExtendedReply(HOME, node, txRadioSeq++ & 15, frame[14],
            h->replyContextRaw, h->targetC100, danfossKey, resp,
            h->configurationKnown ? requestedConfiguration : nullptr);
      } else {
        respLen = Danfoss::buildShortReply(HOME, node, txRadioSeq++ & 15, frame[14],
            h->replyContextRaw, h->targetC100, danfossKey, resp);
      }
      memcpy(h->replyFrame, resp, respLen);
      h->contextUnsent = false; // may reach the head from here on
      h->replyLen = respLen;
      h->replyRevision = revision;
      h->replyAttempts = 0;
      h->replyDeferred = false;
      } else h->replyDeferred = true; // checkpoint must precede application TX
    }
    if (respLen) h->replyAttempts++;
  } else {
    xSemaphoreGive(regMutex);
    return;
  }
  xSemaphoreGive(regMutex);
  Danfoss::buildAck(HOME, node, frame[6], ack);
  uint32_t procUs = micros() - receivedUs;
  int as = linkAlreadyAcked ? 0 : sendFrame(ack, 10);
  int rs = -999;
  if (as == 0 && respLen) {
    delayMicroseconds(1500);
    rs = sendFrame(resp, respLen);
  }
  // The head ACK is only 10 bytes (6 after Home-ID sync). A normal 53-byte
  // receive window can consume it and subsequent noise without yielding a usable
  // packet in time. Listen at the exact ACK length briefly before normal RX.
  if (rs == 0) rxListen(6);
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    h = reg.find(node);
    if (h && rs == 0 && h->replyLen == respLen &&
        h->replyRevision == revision && !memcmp(h->replyFrame, resp, respLen)) {
      h->replies++;
      h->awaitingAck = true;
      h->lastReplyMs = millis();
      // TX alone never clears the pending remote request; wait for its head ACK.
    }
    xSemaphoreGive(regMutex);
  }
  if (rs == 0 && xSemaphoreTake(rxSem, pdMS_TO_TICKS(25)) == pdTRUE) {
    uint8_t rawAck[6] = {}, ackFrame[10];
    int st = radio.readData(rawAck, sizeof rawAck);
    memcpy(ackFrame, HOME, 4);
    for (size_t i = 0; i < sizeof rawAck; i++) ackFrame[4+i] = rawAck[i] ^ 255;
    if (st == 0 && Danfoss::isAck(ackFrame, sizeof ackFrame, HOME)) {
      radioActivityMs = millis();
      if (ackFrame[4] == node && ackFrame[8] == 1 &&
          xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (Bridge::Head *head = reg.find(node)) head->acknowledge(ackFrame[6], millis());
        xSemaphoreGive(regMutex);
      }
      pushEvent(4, ackFrame[4], 0, sizeof ackFrame, radio.getRSSI(), 0, ackFrame);
    }
  }
  // handleRadio restores the normal report receive window on return.
  if (respLen) pushEvent(6, node, 0x10, respLen, 0, procUs, resp, as, rs);
}

void processValid(const uint8_t *frame, size_t n, float rssi, uint32_t receivedUs) {
  uint8_t src = frame[4], dst = frame[8], op = frame[12];
  if (src == 1) {
    // Genuine CC traffic (shadow observation): adopt per-head target/context while
    // we are not the active controller for that head.
    if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      Bridge::Head *h = reg.ensure(dst);
      if (h) {
        h->ccMsgs++;
        if ((op == 0x10 || op == 0x50) && (frame[13] == 0x0a || frame[13] == 0x14) &&
            (frame[14] & 0xf8) == 8 && frame[15] == 0x0d &&
            !h->enabled && !h->pendingExtended) {
          h->learnFromCc(Danfoss::replyContext(frame), Danfoss::replySetpoint(frame),
                         n == 37 ? frame + 22 : nullptr);
        }
      }
      xSemaphoreGive(regMutex);
    }
    pushEvent(2, src, op, n, rssi, 0, frame);
    return;
  }
  Danfoss::ReportKind kind = Danfoss::reportKind(frame, n);
  if (kind == Danfoss::ReportKind::Other) {
    pushEvent(7, src, op, n, rssi, 0, frame);
    return; // authenticated setup/status messages are not thermostat reports
  }
  bool duplicate = false, answer = false;
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    Bridge::Head *h = reg.ensure(src);
    if (h) {
      duplicate = h->observe(frame, n, millis());
      h->rssi10 = int16_t(rssi * 10);
      // A fresh physical action cancels any older queued external target.
      if (!duplicate && kind == Danfoss::ReportKind::Button &&
          Danfoss::reportTarget(frame, n) >= 400 && Danfoss::reportTarget(frame, n) <= 2800) {
        h->localTarget(Danfoss::reportTarget(frame, n), millis());
      }
      answer = h->enabled && h->targetKnown;
    }
    xSemaphoreGive(regMutex);
    if (answer) answerReport(src, frame, duplicate, receivedUs);
  }
  pushEvent(duplicate ? 3 : 1, src, op, n, rssi, 0, frame);
}

void handleRadio() {
  radioBusy = true;
  uint32_t receivedUs = micros();
  uint32_t taskDelayUs = receivedUs - rxSignalUs.load(std::memory_order_relaxed);
  if (taskDelayUs > maxRxTaskDelayUs.load(std::memory_order_relaxed))
    maxRxTaskDelayUs.store(taskDelayUs, std::memory_order_relaxed);
  uint8_t raw[RX_WINDOW] = {}, frame[57];
  int st = radio.readData(raw, RX_WINDOW);
  float rssi = radio.getRSSI();
  frame[0] = HOME[0]; frame[1] = HOME[1]; frame[2] = HOME[2]; frame[3] = HOME[3];
  for (size_t i = 0; i < RX_WINDOW; i++) frame[4 + i] = raw[i] ^ 255;
  size_t declared = frame[7];
  bool ok = st == 0 && declared >= 10 && declared <= sizeof frame;
  if (ok && Danfoss::isAck(frame, declared, HOME)) {
    radioActivityMs = millis();
    if (frame[8] == 1 && xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (Bridge::Head *h = reg.find(frame[4])) h->acknowledge(frame[6], millis());
      xSemaphoreGive(regMutex);
    }
    pushEvent(4, frame[4], 0, declared, rssi, 0, frame);
  } else if (ok && Danfoss::validFrame(frame, declared, danfossKey, HOME)) {
    radioActivityMs = millis();
    processValid(frame, declared, rssi, receivedUs);
  } else {
    // False hardware-sync matches and noise land here; counted, not acted on.
    pushEvent(5, 0, 0, ok ? declared : 0, rssi, 0, nullptr);
  }
  rxListen();
  radioBusy = false;
}

// Idle receiver level, sampled every 2 s between frames. The median of the last
// 31 samples (about a minute) ignores the odd packet caught mid-air.
void sampleNoise() {
  static int16_t samples[31];
  static uint8_t count = 0, next = 0;
  static uint32_t lastMs = 0;
  if (millis() - lastMs < 2000 || radioBusy.load()) return;
  lastMs = millis();
  samples[next] = int16_t(radio.getRSSI(false) * 10);
  next = (next + 1) % 31;
  if (count < 31) ++count;
  int16_t sorted[31];
  memcpy(sorted, samples, sizeof sorted);
  std::sort(sorted, sorted + count);
  BridgeNet::noiseFloor10 = sorted[count / 2];
}

void radioTask(void *) {
  for (;;) {
    if (xSemaphoreTake(rxSem, pdMS_TO_TICKS(2) + 1) == pdTRUE) handleRadio();
    else sampleNoise();
    // The initial link ACK is immediate. Resume an application reply once the
    // loop has committed its ID/target, without waiting for a five-minute wake.
    if (uxSemaphoreGetCount(rxSem)) continue;
    uint8_t frame[57], node = 0;
    if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
      for (auto *h = reg.begin(); h != reg.end(); ++h) {
        if (!h->replyDeferred) continue;
        if (!h->enabled || millis() - h->lastSeenMs > 100) {
          h->replyDeferred = false; continue;
        }
        if (h->replyDurable()) {
          memcpy(frame, h->lastFrame, h->lastFrameLen);
          node = h->node; h->replyDeferred = false; break;
        }
      }
      xSemaphoreGive(regMutex);
    }
    if (node) {
      radioBusy = true;
      answerReport(node, frame, false, micros(), true);
      rxListen();
      radioBusy = false;
    }
  }
}

void printStatus() {
  Bridge::Registry snapshot;
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  snapshot = reg;
  xSemaphoreGive(regMutex);
  Serial.printf("{\"t\":\"status\",\"ms\":%lu,\"heads\":[", millis());
  {
    bool first = true;
    for (Bridge::Head *h = snapshot.begin(); h != snapshot.end(); h++) {
      Serial.printf("%s{\"node\":%u,\"enabled\":%s,\"known\":%s,\"pending\":%s,"
                    "\"target\":%u,\"reply_context_raw\":%lu,\"reply_context_known\":%s,\"configuration_known\":%s,\"reports\":%lu,\"dup\":%lu,\"replies\":%lu,"
                    "\"cc\":%lu,\"reply_acks\":%lu,\"last_seen\":%lu}",
                    first ? "" : ",", h->node, h->enabled ? "true" : "false",
                    h->targetKnown ? "true" : "false", h->pendingExtended ? "true" : "false",
                    h->targetC100, (unsigned long)h->replyContextRaw, h->replyContextKnown ? "true" : "false", h->configurationKnown ? "true" : "false", (unsigned long)h->reports,
                    (unsigned long)h->duplicates, (unsigned long)h->replies,
                    (unsigned long)h->ccMsgs, (unsigned long)h->replyAcks, (unsigned long)h->lastSeenMs);
      first = false;
    }
  }
  Serial.printf("],\"storage\":{\"ready\":%s,\"generation\":%lu,\"autostart\":%s,\"dirty\":%s,\"last_save_us\":%lu,\"max_save_us\":%lu,\"save_failures\":%lu},\"event_queue_drops\":%lu,\"rx_irq_to_task_max_us\":%lu,",
                stateStore.ready() ? "true" : "false", (unsigned long)stateStore.generation(),
                autostart ? "true" : "false", persistenceDirty ? "true" : "false",
                (unsigned long)lastSaveUs, (unsigned long)maxSaveUs, (unsigned long)saveFailures,
                (unsigned long)eventQueueDrops.load(std::memory_order_relaxed),
                (unsigned long)maxRxTaskDelayUs.load(std::memory_order_relaxed));
  BridgeNet::printStatus();
  Serial.println("}");
}

void command(char *line) {
  // Bench restore of genuine CC configuration; never permits control enabling.
  if (!strncmp(line, "IMPORT_CC ", 10)) {
    uint8_t frame[37] = {}; bool ok = strlen(line + 10) == 74;
    for (size_t i = 0; ok && i < sizeof frame; ++i) {
      unsigned value; char pair[3] = {line[10 + 2*i], line[11 + 2*i], 0};
      ok = isxdigit(pair[0]) && isxdigit(pair[1]) && sscanf(pair, "%2x", &value) == 1;
      if (ok) frame[i] = uint8_t(value);
    }
    ok = ok && Danfoss::validFrame(frame, sizeof frame, danfossKey, HOME) &&
         frame[4] == 1 && frame[8] == BridgeMqtt::BENCH_NODE && frame[13] == 0x14 &&
         (frame[12] == 0x10 || frame[12] == 0x50) && (frame[14] & 0xf8) == 8 &&
         frame[15] == 0x0d;
    uint16_t importedTarget = Danfoss::replySetpoint(frame);
    ok = ok && importedTarget >= 400 && importedTarget <= 2800;
    if (ok && xSemaphoreTake(regMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      for (auto *h = reg.begin(); h != reg.end(); ++h) ok &= !h->enabled && !h->pendingExtended;
      auto *h = ok ? reg.ensure(frame[8]) : nullptr;
      if (h) {
        h->learnContext(Danfoss::replyContext(frame), importedTarget);
        h->targetC100 = h->acceptedTargetC100 = importedTarget;
        h->targetKnown = h->acceptedKnown = h->configurationKnown = true;
        memcpy(h->configuration, frame + 22, sizeof h->configuration);
      } else ok = false;
      xSemaphoreGive(regMutex);
    } else ok = false;
    emit("{\"t\":\"import_cc\",\"accepted\":%s}", ok ? "true" : "false");
    return;
  }
  if (!strcmp(line, "SHADOW") || !strncmp(line, "STOP", 4)) {
    if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      emit("{\"t\":\"err\",\"where\":\"shadow\",\"why\":\"registry busy; retry\"}");
      return;
    }
    for (Bridge::Head *h = reg.begin(); h != reg.end(); h++) h->stopControl();
    xSemaphoreGive(regMutex);
    BridgeNet::cancelRequests();
    autostart = false;
    bool saved = saveStateNow(); // all control is off; durably relinquish ownership
    emit("{\"t\":\"mode\",\"shadow\":true,\"recovery_disabled_saved\":%s}", saved ? "true" : "false");
    return;
  }
  if (!strcmp(line, "STATUS")) { printStatus(); return; }
  if (!strcmp(line, "SAVE")) {
    saveRequested = true;
    emit("{\"t\":\"save_queued\"}");
    return;
  }
  if (!strcmp(line, "AUTOSTART ON") || !strcmp(line, "AUTOSTART OFF")) {
    bool on = !strcmp(line, "AUTOSTART ON"), eligible = false;
    if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      for (Bridge::Head *h = reg.begin(); h != reg.end(); h++)
        eligible |= h->enabled && h->acceptedKnown && h->replyContextKnown && h->replyDurable();
      xSemaphoreGive(regMutex);
    }
    if (!stateStore.ready() || (on && !eligible)) {
      emit("{\"t\":\"err\",\"where\":\"autostart\",\"why\":\"requires NVS and an enabled head with an accepted target\"}");
      return;
    }
    autostart = on;
    if (on) {
      saveRequested = true;
      emit("{\"t\":\"autostart\",\"enabled\":true,\"save_queued\":true}");
    } else {
      bool saved = saveStateNow();
      emit("{\"t\":\"autostart\",\"enabled\":false,\"saved\":%s}", saved ? "true" : "false");
    }
    return;
  }
  int node; char onoff[4];
  if (sscanf(line, "CONTROL %d %3s", &node, onoff) == 2 && node > 1 && node < 233) {
    if (strcmp(onoff, "ON") && strcmp(onoff, "OFF")) {
      emit("{\"t\":\"err\",\"where\":\"control\",\"why\":\"use ON or OFF\"}");
      return;
    }
    bool on = !strcmp(onoff, "ON");
    if (on && !BridgeMqtt::controllable(node)) {
      emit("{\"t\":\"err\",\"where\":\"control\",\"why\":\"node is not in the paired-head rollout list\"}");
      return;
    }
    // Events go out after the mutex is released: serial/MQTT output can block,
    // and the radio task only waits 5 ms for the registry before skipping an ACK.
    bool changed = false, unknown = false;
    if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      Bridge::Head *h = reg.ensure(node);
      if (h && on && (!h->targetKnown || !h->replyContextKnown)) {
        unknown = true;
      } else if (h) {
        if (on && !h->enabled) {
          h->enabled = true;
          // A restored binding the head has not acknowledged needs the extended form.
          if (h->targetC100 != h->acceptedTargetC100) {
            h->pendingExtended = true;
            h->pendingSinceMs = millis();
          }
        } else if (!on) h->stopControl();
        changed = true;
      }
      xSemaphoreGive(regMutex);
    }
    if (unknown)
      emit("{\"t\":\"err\",\"where\":\"control\",\"node\":%d,\"why\":\"target or CC context unknown; wait for authenticated CC observation\"}", node);
    if (changed) emit("{\"t\":\"control\",\"node\":%d,\"enabled\":%s}", node, on ? "true" : "false");
    if (changed && !on) {
      if (node == BridgeMqtt::BENCH_NODE) BridgeNet::cancelRequests();
      saveStateNow();
    }
    return;
  }
  float temp;
  if (sscanf(line, "SET %d %f", &node, &temp) == 2 && node > 1 && node < 233) {
    if (!isfinite(temp) || temp < 0 || temp > 2800) {
      emit("{\"t\":\"err\",\"where\":\"set\",\"why\":\"range 4-28 C\"}");
      return;
    }
    uint32_t c100 = temp > 100 ? uint32_t(temp + 0.5f) : uint32_t(temp * 100 + 0.5f);
    if (c100 < 400 || c100 > 2800) {  // datasheet setting range 4-28 C
      emit("{\"t\":\"err\",\"where\":\"set\",\"node\":%d,\"why\":\"range 4-28 C\"}", node);
      return;
    }
    enum { SET_NONE, UNAVAILABLE, QUEUED, EXHAUSTED } outcome = SET_NONE;
    if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      Bridge::Head *h = reg.ensure(node);
      if (h && (!h->enabled || !h->replyContextKnown)) outcome = UNAVAILABLE;
      else if (h) outcome = h->requestTarget(c100, millis()) && h->prepareReply() ? QUEUED : EXHAUSTED;
      xSemaphoreGive(regMutex);
    }
    if (outcome == UNAVAILABLE)
      emit("{\"t\":\"err\",\"where\":\"set\",\"node\":%d,\"why\":\"authenticated CC configuration unavailable\"}", node);
    else if (outcome == QUEUED)
      emit("{\"t\":\"set\",\"node\":%d,\"target\":%lu}", node, (unsigned long)c100);
    else if (outcome == EXHAUSTED)
      emit("{\"t\":\"err\",\"where\":\"set\",\"node\":%d,\"why\":\"manual ID exhausted or context unavailable\"}", node);
    return;
  }
  // Echo only printable ASCII: the line ends up inside JSON on serial and MQTT.
  char shown[41];
  size_t n = 0;
  for (const char *c = line; *c && n < sizeof shown - 1; ++c)
    shown[n++] = (*c >= 32 && *c < 127 && *c != '"' && *c != '\\') ? *c : '?';
  shown[n] = 0;
  emit("{\"t\":\"err\",\"where\":\"cmd\",\"line\":\"%s\"}", shown);
}

void readCommands() {
  static char buf[96];
  static size_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      buf[n] = 0;
      if (n) command(buf);
      n = 0;
    } else if (n < sizeof buf - 1) {
      buf[n++] = ch;
    }
  }
}

void drawStatus() {
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, 0, String("danfoss-") + BridgeEntities::FIRMWARE); // one version string everywhere
  // "Heard" means a report within the HA freshness window, not merely configured.
  uint32_t reports = 0, replies = 0, enabled = 0, heard = 0;
  String silent;
  if (xSemaphoreTake(regMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (Bridge::Head *h = reg.begin(); h != reg.end(); h++) {
      reports += h->reports;
      replies += h->replies;
      if (!h->enabled) continue;
      ++enabled;
      if (h->hasLast && millis() - h->lastSeenMs < BridgeEntities::FRESH_MS) ++heard;
      else silent += (silent.length() ? " " : "") + String(h->node);
    }
    xSemaphoreGive(regMutex);
  }
  oled.drawString(0, 14, "heads heard " + String(heard) + "/" + String(enabled) +
                         (silent.length() ? "  silent: " + silent : ""));
  oled.drawString(0, 26, "reports: " + String(reports));
  oled.drawString(0, 38, "replies: " + String(replies));
  oled.drawString(0, 50, "up " + String(millis() / 60000) + " min  heap " + String(ESP.getFreeHeap() / 1024) + "k");
  oled.display();
}

void setup() {
  Serial.begin(115200);
  BridgeOta::bootCheck(); // a freshly updated image that keeps crashing falls back here
  pinMode(P_VEXT, OUTPUT);
  digitalWrite(P_VEXT, LOW);  // power the OLED
  pinMode(P_OLED_RST, OUTPUT);
  digitalWrite(P_OLED_RST, LOW);
  delay(20);
  digitalWrite(P_OLED_RST, HIGH);
  oled.init();
  oled.flipScreenVertically();

  regMutex = xSemaphoreCreateMutex();
  rxSem = xSemaphoreCreateBinary();
  eventQueue = xQueueCreate(8, sizeof(Event));
  uint8_t keyTag[4];
  const uint8_t storageLabel[8] = {'D', 'F', 'N', 'V', 'S', 0, 0, 1};
  Danfoss::mac(storageLabel, sizeof storageLabel, danfossKey, keyTag);
  Bridge::SavedState recovered;
  bool restored = stateStore.begin(HOME, keyTag) && stateStore.load(recovered) &&
                  Bridge::restoreState(recovered, reg);
  // V3 binds an issued ID to its target before transmission. V1/V2 migrate
  // passively because they cannot reconstruct that binding after a lost ACK.
  autostart = restored && recovered.autostart;
  for (uint8_t node : KNOWN_NODES) reg.ensure(node);

  spi.begin(P_SCK, P_MISO, P_MOSI, P_NSS);
  int st = radio.beginFSK(RF_FREQ, RF_BR, RF_DEV, RF_RXBW, RF_POWER, 80, TCXO_V);
  if (st == RADIOLIB_ERR_NONE) {
    // Keep the crystal running across TX/RX turnarounds: restarting the TCXO
    // can miss a head's immediate link ACK after our application reply.
    radio.setStandbyXOSC(true);
    radio.setDio2AsRfSwitch(true);
    radio.setRxBoostedGainMode(true);
    radio.setCRC(0);
    radio.setWhitening(false);
    radio.setDataShaping(RADIOLIB_SHAPING_NONE);
    rxListen();
  }
  BridgeNet::radioReady = st == RADIOLIB_ERR_NONE;
  bool controlling = false;
  for (Bridge::Head *h = reg.begin(); h != reg.end(); h++) controlling |= h->enabled;
  emit("{\"t\":\"boot\",\"fw\":\"%s\",\"radio\":%s,\"freq\":%.4f,\"shadow\":%s,\"recovered\":%s,\"autostart\":%s}",
                BridgeEntities::FIRMWARE, st == RADIOLIB_ERR_NONE ? "true" : "false", RF_FREQ,
                controlling ? "false" : "true", restored ? "true" : "false", autostart ? "true" : "false");
  xTaskCreatePinnedToCore(radioTask, "radio", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
  BridgeNet::begin();
  drawStatus();
}

void loop() {
  readCommands();
  diag.storageReady = stateStore.ready();
  diag.generation = stateStore.generation();
  diag.saveFailures = saveFailures;
  diag.autostart = autostart;
  diag.eventDrops = eventQueueDrops.load(std::memory_order_relaxed);
  BridgeNet::poll(reg, regMutex, diag);
  persistenceTick();
  if (BridgeNet::restartRequested.load() && !radioBusy.load()) {
    // Same path as a reset button press, but never with an unsaved accepted target.
    if (persistenceDirty) saveStateNow();
    BridgeNet::restartRequested = false;
    if (persistenceDirty) {
      emit("{\"t\":\"err\",\"where\":\"restart\",\"why\":\"checkpoint not saved; restart refused\"}");
    } else {
      emit("{\"t\":\"restart\",\"source\":\"mqtt\"}");
      delay(500); // let the event leave before the broker sees the LWT
      ESP.restart();
    }
  }
  Event e;
  while (xQueueReceive(eventQueue, &e, 0) == pdTRUE) {
    static const char *names[] = {"?", "report", "cc", "dup", "ack", "invalid", "tx", "other"};
    char line[400];
    size_t n = snprintf(line, sizeof line, "{\"t\":\"%s\",\"ms\":%lu,\"queue_delay_ms\":%lu,\"node\":%u,\"op\":%u,\"len\":%u,\"rssi\":%.1f",
                        names[e.type <= 7 ? e.type : 0], (unsigned long)e.queuedMs,
                        (unsigned long)(millis() - e.queuedMs), e.node, e.op, e.len, e.rssi10 / 10.0f);
    if (e.type == 6) n += snprintf(line + n, sizeof line - n, ",\"ack_status\":%d,\"response_status\":%d", e.ackStatus, e.responseStatus);
    if (e.type == 1 || e.type == 3)
      n += snprintf(line + n, sizeof line - n, ",\"status_raw\":\"%04x\"", Danfoss::reportStatusRaw(e.frame, e.len));
    if (e.procUs) n += snprintf(line + n, sizeof line - n, ",\"processing_us\":%lu", (unsigned long)e.procUs);
    size_t withoutHex = n;
    if (e.len) {
      n += snprintf(line + n, sizeof line - n, ",\"hex\":\"");
      for (size_t i = 0; i < e.len && i < sizeof e.frame; ++i)
        n += snprintf(line + n, sizeof line - n, "%02x", e.frame[i]);
      n += snprintf(line + n, sizeof line - n, "\"");
    }
    snprintf(line + n, sizeof line - n, "}");
    Serial.println(line);
    // Noise is only counted. Op 80 (inclusion) frames carry the network key in
    // clear: the MQTT copy drops their hex.
    if (e.type == 5) continue;
    if (e.op == 0x80) snprintf(line + withoutHex, sizeof line - withoutHex, "}");
    BridgeNet::frame(line);
  }
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw >= 2000) {
    lastDraw = millis();
    drawStatus();
  }
  delay(5);
}
