#pragma once
// Network work stays outside the radio task. MQTT can request targets and a
// restart, but cannot enable control or change boot ownership.
#include <WiFi.h>
#include <mqtt_client.h>
#include <esp_system.h>
#include <time.h>
#include <atomic>
#include "registry.h"
#include "mqtt_protocol.h"
#include "household.h"
#include "mqtt_entities.h"
#include "ota.h"

#if __has_include("network_config.private.h")
#include "network_config.private.h"
#else
static const char *NET_SSID = "", *NET_PASSWORD = "";
static const char *NET_MQTT_HOST = "", *NET_MQTT_USER = "", *NET_MQTT_PASSWORD = "", *NET_OTA_PASSWORD = "";
static const uint16_t NET_MQTT_PORT = 1883;
#endif


namespace BridgeNet {
struct Request { uint8_t node; uint16_t target; uint32_t atMs, epoch, id; };
// Loop-task health copied into each snapshot for the bridge state topic.
struct Diag {
  uint32_t generation = 0, saveFailures = 0, eventDrops = 0, lastErrorMs = 0;
  bool storageReady = false, autostart = false, hasError = false;
  char lastError[160] = ""; // the most recent err event, verbatim JSON
};
struct Snapshot { Bridge::Registry heads; Diag diag; uint32_t seq = 0; };
static QueueHandle_t requests = nullptr, snapshots = nullptr;
static esp_mqtt_client_handle_t client = nullptr;
static std::atomic<bool> connected{false}, refresh{false}, started{false}, restartRequested{false};
static std::atomic<uint32_t> epoch{0}, dropped{0};
// Snapshot that must be published at once (a queued command), not at the next 15 s round.
static std::atomic<uint32_t> urgentSeq{0};
static std::atomic<bool> radioReady{false};   // set by setup() when the SX1262 initialised
static std::atomic<int16_t> noiseFloor10{0}; // radio task: median idle RSSI, dBm*10; 0 = not yet measured
static uint32_t nextId = 0;
static bool advertised[233] = {}; // network task only: retained discovery published

static bool publish(const char *topic, const char *payload, bool retain = true) {
  if (!connected.load() || esp_mqtt_client_get_outbox_size(client) > 8192) return false;
  return esp_mqtt_client_enqueue(client, topic, payload, 0, 0, retain, true) >= 0;
}
static esp_err_t event(esp_mqtt_event_handle_t e) {
  if (e->event_id == MQTT_EVENT_CONNECTED) {
    epoch.fetch_add(1);
    connected = true;
    esp_mqtt_client_subscribe(client, BridgeMqtt::SET_FILTER, 0);
    esp_mqtt_client_subscribe(client, "homeassistant/status", 0);
    esp_mqtt_client_subscribe(client, BridgeEntities::BRIDGE_COMMAND, 0);
    refresh = true;
  } else if (e->event_id == MQTT_EVENT_DISCONNECTED) {
    connected = false;
  } else if (e->event_id == MQTT_EVENT_DATA) {
    // Ignore stale retained commands, retransmissions and fragmented payloads.
    if (e->current_data_offset || e->total_data_len != e->data_len) return ESP_OK;
    if (e->topic_len == 20 && !memcmp(e->topic, "homeassistant/status", 20)) {
      if (e->data_len == 6 && !memcmp(e->data, "online", 6)) refresh = true;
      return ESP_OK;
    }
    size_t commandLen = strlen(BridgeEntities::BRIDGE_COMMAND);
    if (e->topic_len == int(commandLen) && !memcmp(e->topic, BridgeEntities::BRIDGE_COMMAND, commandLen)) {
      if (!e->retain && e->data_len == 7 && !memcmp(e->data, "RESTART", 7)) restartRequested = true;
      return ESP_OK;
    }
    uint16_t target;
    if (!BridgeMqtt::command(e->topic, e->topic_len, e->data, e->data_len,
                            e->retain, e->dup, e->current_data_offset,
                            e->total_data_len, target)) return ESP_OK;
    uint8_t node;
    if (!BridgeMqtt::commandNode(e->topic, e->topic_len, node)) return ESP_OK;
    Request r{node, target, millis(), epoch.load(), ++nextId};
    if (xQueueSend(requests, &r, 0) != pdTRUE) dropped.fetch_add(1);
  }
  return ESP_OK;
}

// Direct QoS0 publishing is confined to this low-priority network task. It
// cannot accumulate an outbox of stale telemetry while the broker is slow.
static bool publishTask(const char *topic, const char *payload) {
  return connected.load() && esp_mqtt_client_publish(client, topic, payload, 0, 0, true) >= 0;
}
static bool discovery(Snapshot &s) {
  char topic[128], body[2048];
#define RETIRE_DISCOVERY_(n) ok &= publishTask("homeassistant/climate/" DANFOSS_UID "/" #n "/config", "");
  bool ok = true;
  HOUSEHOLD_RETIRED_NODES(RETIRE_DISCOVERY_)
  for (const char *kind : BridgeEntities::BRIDGE_KINDS) {
    snprintf(topic, sizeof topic, "homeassistant/%s/" DANFOSS_UID "/bridge_%s/config", BridgeEntities::domain(kind), kind);
    if (!BridgeEntities::bridgeDiscovery(kind, body, sizeof body) || !publishTask(topic, body)) ok = false;
  }
  for (auto *h = s.heads.begin(); h != s.heads.end(); ++h) {
    if (Household::retired(h->node)) continue;
    bool headOk = true;
    for (const char *kind : BridgeEntities::HEAD_KINDS) {
      bool climate = !strcmp(kind, "climate");
      if (climate && !BridgeMqtt::controllable(h->node)) continue;
      const char *domain = BridgeEntities::domain(kind);
      if (climate) snprintf(topic, sizeof topic, "homeassistant/%s/" DANFOSS_UID "/%u/config", domain, h->node);
      else snprintf(topic, sizeof topic, "homeassistant/%s/" DANFOSS_UID "/%u_%s/config", domain, h->node, kind);
      if (!BridgeEntities::discovery(h->node, kind, body, sizeof body) || !publishTask(topic, body)) headOk = false;
    }
    advertised[h->node] = headOk;
    ok &= headOk;
  }
  return ok;
}

static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT: return "external";
    default: return "other";
  }
}

static BridgeEntities::Clock clock;
static uint32_t startedEpoch = 0;
// SNTP runs in the background; adopt (and every 10 days refresh) one fixed base.
static void updateClock() {
  if (clock.baseEpoch && millis() - clock.baseMs < 864000000UL) return;
  time_t t = time(nullptr);
  if (t < 1700000000) return;
  clock.baseEpoch = uint32_t(t); clock.baseMs = millis();
  if (!startedEpoch) startedEpoch = clock.baseEpoch - clock.baseMs / 1000;
}

static bool bridgeState(Snapshot &s, char *out, size_t capacity) {
  char started[32] = "null", errorAt[32] = "null", noise[16] = "null";
  if (noiseFloor10.load()) snprintf(noise, sizeof noise, "%d", (noiseFloor10.load() - 5) / 10);
  if (startedEpoch) BridgeEntities::isoEpoch(startedEpoch, started, sizeof started);
  if (s.diag.hasError) BridgeEntities::isoTime(clock, s.diag.lastErrorMs, millis(), errorAt, sizeof errorAt);
  unsigned controlled = 0;
  for (auto *h = s.heads.begin(); h != s.heads.end(); ++h) controlled += h->enabled;
  int n = snprintf(out, capacity,
    "{\"fw\":\"%s\",\"started\":%s,\"reset_reason\":\"%s\",\"wifi_rssi\":%d,\"min_free_heap_kb\":%lu,"
    "\"storage_ready\":%s,\"checkpoint_generation\":%lu,\"save_failures\":%lu,\"autostart\":%s,"
    "\"controlled_heads\":%u,\"dropped\":%lu,\"last_error\":%s,\"last_error_at\":%s,"
    "\"noise_floor\":%s,\"ip\":\"%s\",\"partition\":\"%s\"}",
    BridgeEntities::FIRMWARE, started, resetReason(), int(WiFi.RSSI()), (unsigned long)(ESP.getMinFreeHeap() / 1024),
    s.diag.storageReady ? "true" : "false", (unsigned long)s.diag.generation, (unsigned long)s.diag.saveFailures,
    s.diag.autostart ? "true" : "false", controlled,
    (unsigned long)(s.diag.eventDrops + dropped.load()), s.diag.hasError ? s.diag.lastError : "null", errorAt,
    noise, WiFi.localIP().toString().c_str(), BridgeOta::runningLabel());
  return n >= 0 && size_t(n) < capacity;
}

static void task(void *) {
  WiFi.persistent(false); // credentials are provisioned in the private build file
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(NET_SSID, NET_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com"); // timestamps for HA only
  esp_mqtt_client_config_t config = {};
  config.host = NET_MQTT_HOST; config.port = NET_MQTT_PORT;
  config.username = NET_MQTT_USER; config.password = NET_MQTT_PASSWORD;
  config.client_id = DANFOSS_UID;
  config.event_handle = event;
  config.lwt_topic = DANFOSS_ROOT "/availability";
  config.lwt_msg = "offline"; config.lwt_retain = 1;
  config.keepalive = 30; config.reconnect_timeout_ms = 5000;
  config.network_timeout_ms = 2000; config.buffer_size = 2048;
  client = esp_mqtt_client_init(&config);
  if (!client || esp_mqtt_client_start(client) != ESP_OK) {
    vTaskDelete(nullptr);
    return;
  }
  started = true;
  Snapshot s;
  uint32_t lastPublish = 0;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) BridgeOta::begin(NET_OTA_PASSWORD);
    if (BridgeOta::tick(connected.load() && radioReady.load())) {
      Serial.println("{\"t\":\"ota_confirmed\"}");
      publish(BridgeEntities::BRIDGE_EVENT, "{\"t\":\"ota_confirmed\"}", false);
    }
    xQueueReceive(snapshots, &s, pdMS_TO_TICKS(200));
    // Wait for a real registry snapshot before advertising entities.
    if (s.heads.begin() == s.heads.end()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    // A head first heard after the last resync still needs its entities.
    for (auto *h = s.heads.begin(); h != s.heads.end(); ++h)
      if (!Household::retired(h->node) && !advertised[h->node]) refresh = true;
    updateClock();
    bool resync = refresh.exchange(false);
    static uint32_t urgentDone = 0;
    uint32_t urgent = urgentSeq.load();
    bool soon = urgent != urgentDone && s.seq - urgent < 0x80000000u; // this snapshot includes it
    if (soon) urgentDone = urgent;
    if (connected.load() && (resync || soon || millis() - lastPublish >= 15000)) {
      lastPublish = millis();
      if (resync && !discovery(s)) refresh = true;
      publishTask(DANFOSS_ROOT "/availability", "online");
      char bridge[768];
      if (bridgeState(s, bridge, sizeof bridge)) publishTask(BridgeEntities::BRIDGE_STATE, bridge);
      for (auto *h = s.heads.begin(); h != s.heads.end(); ++h) {
        if (Household::retired(h->node)) continue;
        char topic[128], body[768];
        bool fresh = h->hasLast && millis() - h->lastSeenMs < BridgeEntities::FRESH_MS;
        snprintf(topic, sizeof topic, DANFOSS_ROOT "/head/%u/telemetry_availability", h->node);
        publishTask(topic, fresh ? "online" : "offline");
        if (BridgeMqtt::controllable(h->node)) {
          snprintf(topic, sizeof topic, DANFOSS_ROOT "/head/%u/availability", h->node);
          // Not tied to freshness: a rarely heard head still takes a target, sent at its next heard wake.
          publishTask(topic, h->enabled && h->acceptedKnown && h->replyDurable() ? "online" : "offline");
        }
        if (BridgeEntities::state(*h, millis(), clock, body, sizeof body)) {
          snprintf(topic, sizeof topic, DANFOSS_ROOT "/head/%u/state", h->node);
          publishTask(topic, body);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

inline void begin() {
  if (!NET_SSID[0] || !NET_MQTT_HOST[0] || !NET_MQTT_USER[0] || !NET_MQTT_PASSWORD[0]) return;
  requests = xQueueCreate(8, sizeof(Request));
  snapshots = xQueueCreate(1, sizeof(Snapshot));
  if (!requests || !snapshots) return;
  xTaskCreatePinnedToCore(task, "network", 16384, nullptr, 1, nullptr, 0);
}

inline void cancelRequests() {
  if (requests) xQueueReset(requests);
}

// Called by console loop. A command must still be current and explicitly enabled
// when dequeued. A local action since MQTT arrival supersedes it even if delayed.
inline void poll(Bridge::Registry &reg, SemaphoreHandle_t mutex, const Diag &diag) {
  if (!requests || !snapshots) return;
  static bool urgent = false; // a queued command: snapshot and publish now
  Request r;
  if (xQueueReceive(requests, &r, 0) == pdTRUE) {
    const char *result = "rejected";
    if (connected.load() && r.epoch == epoch.load() && millis() - r.atMs < 5000 &&
        xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      Bridge::Head *h = reg.find(r.node);
      if (h && h->enabled && h->replyContextKnown &&
          !BridgeMqtt::supersededByLocal(h->localActionSeen, h->lastLocalMs, r.atMs, millis())) {
        if (h->requestTarget(r.target, millis()) && h->prepareReply()) { result = "queued"; urgent = true; }
      }
      xSemaphoreGive(mutex);
    }
    char line[128];
    snprintf(line, sizeof line, "{\"t\":\"mqtt_command\",\"id\":%lu,\"node\":%u,\"target\":%u,\"result\":\"%s\"}",
             (unsigned long)r.id, r.node, r.target, result);
    Serial.println(line);
    publish(BridgeEntities::BRIDGE_EVENT, line, false);
    char receipt[128];
    snprintf(receipt, sizeof receipt, "{\"id\":%lu,\"target\":%u,\"result\":\"%s\"}",
             (unsigned long)r.id, r.target, result);
    char receiptTopic[96];
    snprintf(receiptTopic, sizeof receiptTopic, DANFOSS_ROOT "/head/%u/command_result", r.node);
    publish(receiptTopic, receipt, false);
  }
  static uint32_t lastSnapshot = 0, seq = 0;
  if (!urgent && millis() - lastSnapshot < 1000) return;
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
  // Forget samples older than ~12 days: every age below then stays far from the
  // 24.8/49.7-day millis() wrap, so a long-silent head never looks fresh again.
  uint32_t now = millis();
  for (auto *h = reg.begin(); h != reg.end(); ++h) {
    const uint32_t ANCIENT = 0x40000000;
    if (h->hasLast && now - h->lastSeenMs > ANCIENT) h->hasLast = false;
    if (h->roomKnown && now - h->roomSeenMs > ANCIENT) h->roomKnown = false;
    if (h->statusKnown && now - h->statusSeenMs > ANCIENT) h->statusKnown = false;
    if (h->localActionSeen && now - h->lastLocalMs > ANCIENT) h->localActionSeen = false;
  }
  Snapshot s;
  s.heads = reg;
  xSemaphoreGive(mutex);
  s.diag = diag;
  s.seq = ++seq;
  xQueueOverwrite(snapshots, &s);
  if (urgent) urgentSeq = s.seq; // after the overwrite: the network task sees it with this snapshot
  urgent = false;
  lastSnapshot = millis();
}
// Event log and radio frames for monitoring without the serial console. Never
// retained. Frames that can carry the network key in clear are never passed here.
inline void event(const char *line) { publish(BridgeEntities::BRIDGE_EVENT, line, false); }
inline void frame(const char *line) { publish(DANFOSS_ROOT "/debug/frame", line, false); }

inline void printStatus() {
  Serial.printf("\"network\":{\"configured\":%s,\"started\":%s,\"wifi\":%s,\"mqtt\":%s,\"command_queue_drops\":%lu}",
                requests && snapshots ? "true" : "false", started.load() ? "true" : "false", WiFi.status() == WL_CONNECTED ? "true" : "false",
                connected.load() ? "true" : "false", (unsigned long)dropped.load());
}
}
