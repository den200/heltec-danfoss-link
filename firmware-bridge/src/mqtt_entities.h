#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "registry.h"
#include "household.h"
namespace BridgeEntities {
static constexpr const char *FIRMWARE = "bridge-0.15.6";
static constexpr uint32_t FRESH_MS = 900000;
#define HOUSEHOLD_ROOM_CASE_(n, room) case n: return room;
inline const char *roomName(uint8_t node) {
  switch (node) {
    HOUSEHOLD_HEADS(HOUSEHOLD_ROOM_CASE_)
    default: return "Unmapped";
  }
}

// Maps millis() instants to UTC once NTP has synchronized. Every timestamp is
// derived from one fixed base, so republishing an unchanged sample yields the
// same text and HA records nothing new.
struct Clock { uint32_t baseEpoch = 0, baseMs = 0; };
inline void isoEpoch(uint32_t epoch, char *out, size_t capacity) {
  time_t t = time_t(epoch);
  struct tm tm;
  gmtime_r(&t, &tm);
  strftime(out, capacity, "\"%Y-%m-%dT%H:%M:%SZ\"", &tm);
}
// `ms` is a past instant. Both ages are taken from `now`, so neither the base
// (at most 10 days old) nor the instant wraps; instants older than ~12 days
// are not published at all.
inline void isoTime(const Clock &c, uint32_t ms, uint32_t now, char *out, size_t capacity) {
  if (!c.baseEpoch || now - ms > 0x40000000) { snprintf(out, capacity, "null"); return; }
  int64_t sinceBase = int64_t(uint32_t(now - c.baseMs)) - int64_t(uint32_t(now - ms));
  isoEpoch(uint32_t(int64_t(c.baseEpoch) + sinceBase / 1000), out, capacity);
}

// Remaining manual updates before the reply context's low byte fails closed.
inline int updatesLeft(const Bridge::Head &h) {
  return h.replyContextKnown ? 255 - int(h.replyContextRaw & 255) : -1;
}

// No ticking ages: the payload changes only when the head reports or its state
// changes, so the 15 s republish costs HA's recorder nothing.
inline bool state(Bridge::Head &h, uint32_t now, const Clock &clock, char *out, size_t capacity) {
  char target[24] = "null", desired[24] = "null", room[24] = "null", rssi[16] = "null", left[8] = "null";
  char seen[32] = "null", roomAt[32] = "null", pendingAt[32] = "null";
  char front[16] = "null", valve[16] = "null", opening[16] = "null", word46[8] = "null", level[8] = "null";
  if (h.acceptedKnown) snprintf(target, sizeof target, "%.2f", h.acceptedTargetC100 / 100.0);
  if (h.targetKnown) snprintf(desired, sizeof desired, "%.2f", h.targetC100 / 100.0);
  if (h.hasLast) {
    isoTime(clock, h.lastSeenMs, now, seen, sizeof seen);
    snprintf(rssi, sizeof rssi, "%d", (h.rssi10 - 5) / 10);
  }
  if (h.roomKnown) isoTime(clock, h.roomSeenMs, now, roomAt, sizeof roomAt);
  if (h.roomKnown && now - h.roomSeenMs < FRESH_MS) {
    snprintf(room, sizeof room, "%.2f", h.roomC100 / 100.0);
    snprintf(front, sizeof front, "%.2f", h.frontC100 / 100.0);
    snprintf(valve, sizeof valve, "%.2f", h.valveC100 / 100.0);
    if (h.valvePermille <= 1000) snprintf(opening, sizeof opening, "%.1f", h.valvePermille / 10.0);
    snprintf(word46, sizeof word46, "\"%04x\"", h.word46);
  }
  if (h.pendingExtended) isoTime(clock, h.pendingSinceMs, now, pendingAt, sizeof pendingAt);
  if (updatesLeft(h) >= 0) snprintf(left, sizeof left, "%d", updatesLeft(h));
  bool statusFresh = h.statusKnown && h.hasLast && now - h.statusSeenMs < FRESH_MS;
  int8_t low = statusFresh ? Danfoss::batteryLow(h.statusRaw) : -1;
  if (statusFresh && Danfoss::batteryLevel(h.statusRaw) >= 0) snprintf(level, sizeof level, "%d", Danfoss::batteryLevel(h.statusRaw));
  int n = snprintf(out, capacity,
    "{\"node\":%u,\"room\":\"%s\",\"room_mapping\":\"%s\",\"accepted_target\":%s,\"requested_target\":%s,\"room_temperature\":%s,"
    "\"room_temperature_estimated\":true,\"room_updated\":%s,\"front_temperature\":%s,\"valve_temperature\":%s,"
    "\"valve_opening\":%s,\"report_word46\":%s,\"low_battery\":%s,\"battery_level\":%s,"
    "\"status_raw\":\"%04x\",\"pending\":%s,\"pending_since\":%s,\"last_seen\":%s,\"rssi\":%s,\"updates_left\":%s,"
    "\"control_enabled\":%s,\"configuration_known\":%s,\"hvac_mode\":\"heat\"}",
    h.node, roomName(h.node), h.node == 5 ? "inferred" : "matched", target, desired, room, roomAt,
    front, valve, opening, word46, low < 0 ? "null" : low ? "true" : "false", level, h.statusRaw,
    h.pendingExtended ? "true" : "false", pendingAt, seen, rssi, left, h.enabled ? "true" : "false",
    h.configurationKnown ? "true" : "false");
  return n >= 0 && size_t(n) < capacity;
}

static const char *const HEAD_KINDS[] = {"climate", "room", "setpoint", "battery", "last_seen", "rssi", "updates_left",
                                         "valve_opening", "valve_temperature", "battery_level"};
inline const char *domain(const char *kind) {
  if (!strcmp(kind, "climate")) return "climate";
  if (!strcmp(kind, "battery") || !strcmp(kind, "autostart")) return "binary_sensor";
  if (!strcmp(kind, "restart")) return "button";
  return "sensor";
}

// Fields shared by a plain diagnostic sensor reading one JSON key.
inline int sensorFields(char *out, size_t capacity, const char *topic, const char *key, const char *extra) {
  return snprintf(out, capacity,
    "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.%s }}\"%s%s",
    topic, key, extra[0] ? "," : "", extra);
}

// Each head is its own device under the bridge; control availability requires ownership.
inline bool discovery(uint8_t node, const char *kind, char *out, size_t capacity) {
  // HA prefixes entity names with the device (head) name. The climate entity is
  // the head's main feature and takes the device name alone (JSON null).
  char topic[64];
  snprintf(topic, sizeof topic, DANFOSS_ROOT "/head/%u/state", node);
  bool climate = !strcmp(kind, "climate");
  const char *entityName;
  char fields[1100];
  int f;
  if (climate) {
    entityName = "null";
    f = snprintf(fields, sizeof fields,
      "\"modes\":[\"heat\"],\"mode_state_topic\":\"%s\","
      "\"mode_state_template\":\"{{ value_json.hvac_mode }}\","
      "\"temperature_command_topic\":\"" DANFOSS_ROOT "/head/%u/set\","
      "\"temperature_state_topic\":\"%s\","
      // The requested value, so the app does not jump back while a sleeping head
      // waits for delivery; `pending` and the Setpoint sensor show what it accepted.
      "\"temperature_state_template\":\"{{ value_json.requested_target if value_json.requested_target is not none else value_json.accepted_target }}\","
      "\"current_temperature_topic\":\"%s\","
      "\"current_temperature_template\":\"{{ value_json.room_temperature }}\","
      "\"json_attributes_topic\":\"%s\","
      "\"min_temp\":4,\"max_temp\":28,\"temp_step\":0.5,\"precision\":0.1,\"temperature_unit\":\"C\",\"retain\":false",
      topic, node, topic, topic, topic);
  } else if (!strcmp(kind, "battery")) {
    entityName = "\"Low battery\"";
    f = snprintf(fields, sizeof fields,
      "\"device_class\":\"battery\",\"entity_category\":\"diagnostic\","
      "\"state_topic\":\"%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"value_template\":\"{{ 'ON' if value_json.low_battery == true else 'OFF' if value_json.low_battery == false else 'None' }}\"", topic);
  } else if (!strcmp(kind, "room") || !strcmp(kind, "setpoint")) {
    bool room = !strcmp(kind, "room");
    entityName = room ? "\"Room temperature estimate\"" : "\"Setpoint\"";
    f = snprintf(fields, sizeof fields,
      "\"device_class\":\"temperature\",\"state_class\":\"measurement\",\"unit_of_measurement\":\"°C\","
      "\"suggested_display_precision\":1,\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.%s }}\"",
      topic, room ? "room_temperature" : "accepted_target");
  } else if (!strcmp(kind, "last_seen")) {
    entityName = "\"Last report\"";
    f = sensorFields(fields, sizeof fields, topic, "last_seen", "\"device_class\":\"timestamp\"");
  } else if (!strcmp(kind, "rssi")) {
    entityName = "\"Signal strength\"";
    f = sensorFields(fields, sizeof fields, topic, "rssi",
      "\"device_class\":\"signal_strength\",\"unit_of_measurement\":\"dBm\",\"state_class\":\"measurement\"");
  } else if (!strcmp(kind, "valve_opening")) {
    entityName = "\"Valve opening\"";
    f = snprintf(fields, sizeof fields,
      "\"unit_of_measurement\":\"%%\",\"state_class\":\"measurement\",\"icon\":\"mdi:valve\","
      "\"suggested_display_precision\":0,\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.valve_opening }}\"", topic);
  } else if (!strcmp(kind, "valve_temperature")) {
    entityName = "\"Valve-side temperature\"";
    f = snprintf(fields, sizeof fields,
      "\"device_class\":\"temperature\",\"state_class\":\"measurement\",\"unit_of_measurement\":\"°C\","
      "\"suggested_display_precision\":1,\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.valve_temperature }}\"", topic);
  } else if (!strcmp(kind, "battery_level")) {
    entityName = "\"Battery level\"";
    f = sensorFields(fields, sizeof fields, topic, "battery_level",
      "\"device_class\":\"battery\",\"unit_of_measurement\":\"%\",\"state_class\":\"measurement\"");
  } else if (!strcmp(kind, "updates_left")) {
    entityName = "\"Setpoint updates left\"";
    f = sensorFields(fields, sizeof fields, topic, "updates_left", "\"state_class\":\"measurement\",\"icon\":\"mdi:counter\"");
  } else return false;
  if (f < 0 || size_t(f) >= sizeof fields) return false;
  char headName[96];
  if (node == 2 || node == 6) snprintf(headName, sizeof headName, "%s (node %u)", roomName(node), node);
  else snprintf(headName, sizeof headName, "%s", roomName(node));
  int n = snprintf(out, capacity,
    "{\"name\":%s,\"unique_id\":\"" DANFOSS_UID "_%u_%s\","
    "\"device\":{\"identifiers\":[\"" DANFOSS_UID "_head_%u\"],\"name\":\"%s\","
    "\"manufacturer\":\"Danfoss\",\"model\":\"Link radiator head\",\"suggested_area\":\"%s\",\"via_device\":\"" DANFOSS_UID "\"},"
    "\"availability\":[{\"topic\":\"" DANFOSS_ROOT "/availability\"},"
    "{\"topic\":\"" DANFOSS_ROOT "/head/%u/%s\"}],\"availability_mode\":\"all\",%s}",
    entityName, node, kind, node, headName, roomName(node), node, climate ? "availability" : "telemetry_availability", fields);
  return n >= 0 && size_t(n) < capacity;
}

// The bridge's own health, so it can be monitored without the serial console.
static const char *const BRIDGE_KINDS[] = {"started", "reset_reason", "wifi_rssi", "min_free_heap", "checkpoint",
                                           "save_failures", "dropped", "autostart", "last_error", "last_event", "restart",
                                           "noise_floor", "ip"};
static constexpr const char *BRIDGE_STATE = DANFOSS_ROOT "/bridge/state";
static constexpr const char *BRIDGE_EVENT = DANFOSS_ROOT "/bridge/event";
static constexpr const char *BRIDGE_COMMAND = DANFOSS_ROOT "/bridge/command";
inline bool bridgeDiscovery(const char *kind, char *out, size_t capacity) {
  const char *name;
  char fields[700];
  int f;
  if (!strcmp(kind, "started")) {
    name = "Started";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "started", "\"device_class\":\"timestamp\"");
  } else if (!strcmp(kind, "reset_reason")) {
    name = "Reset reason";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "reset_reason", "");
  } else if (!strcmp(kind, "wifi_rssi")) {
    name = "WiFi signal";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "wifi_rssi",
      "\"device_class\":\"signal_strength\",\"unit_of_measurement\":\"dBm\",\"state_class\":\"measurement\"");
  } else if (!strcmp(kind, "min_free_heap")) {
    name = "Lowest free memory";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "min_free_heap_kb",
      "\"device_class\":\"data_size\",\"unit_of_measurement\":\"kB\",\"state_class\":\"measurement\"");
  } else if (!strcmp(kind, "checkpoint")) {
    name = "Checkpoint generation";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "checkpoint_generation", "\"icon\":\"mdi:content-save\"");
  } else if (!strcmp(kind, "save_failures")) {
    name = "Checkpoint failures";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "save_failures", "\"icon\":\"mdi:content-save-alert\"");
  } else if (!strcmp(kind, "dropped")) {
    name = "Dropped events";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "dropped", "\"icon\":\"mdi:tray-alert\"");
  } else if (!strcmp(kind, "autostart")) {
    name = "Autostart";
    f = snprintf(fields, sizeof fields,
      "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"value_template\":\"{{ 'ON' if value_json.autostart else 'OFF' }}\"", BRIDGE_STATE);
  } else if (!strcmp(kind, "last_error")) {
    name = "Last error";
    f = snprintf(fields, sizeof fields,
      "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\",\"icon\":\"mdi:alert-circle-outline\","
      "\"value_template\":\"{{ (value_json.last_error.where ~ ': ' ~ (value_json.last_error.why or value_json.last_error.line or '?'))[:250] if value_json.last_error else 'none' }}\","
      "\"json_attributes_topic\":\"%s\",\"json_attributes_template\":\"{{ {'at': value_json.last_error_at, 'detail': value_json.last_error} | tojson }}\"",
      BRIDGE_STATE, BRIDGE_STATE);
  } else if (!strcmp(kind, "last_event")) {
    name = "Last event";
    f = snprintf(fields, sizeof fields,
      "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.t }}\","
      "\"json_attributes_topic\":\"%s\",\"icon\":\"mdi:text-box-outline\"", BRIDGE_EVENT, BRIDGE_EVENT);
  } else if (!strcmp(kind, "noise_floor")) {
    name = "Radio noise floor";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "noise_floor",
      "\"device_class\":\"signal_strength\",\"unit_of_measurement\":\"dBm\",\"state_class\":\"measurement\"");
  } else if (!strcmp(kind, "ip")) {
    name = "IP address";
    f = sensorFields(fields, sizeof fields, BRIDGE_STATE, "ip", "\"icon\":\"mdi:ip-network\"");
  } else if (!strcmp(kind, "restart")) {
    name = "Restart";
    f = snprintf(fields, sizeof fields,
      "\"device_class\":\"restart\",\"entity_category\":\"config\",\"command_topic\":\"%s\","
      "\"payload_press\":\"RESTART\",\"retain\":false", BRIDGE_COMMAND);
  } else return false;
  if (f < 0 || size_t(f) >= sizeof fields) return false;
  int n = snprintf(out, capacity,
    "{\"name\":\"%s\",\"unique_id\":\"" DANFOSS_UID "_bridge_%s\","
    "\"device\":{\"identifiers\":[\"" DANFOSS_UID "\"],\"name\":\"Danfoss Heltec\","
    "\"manufacturer\":\"Heltec\",\"model\":\"WiFi LoRa 32 V3 (Danfoss Link bridge)\",\"sw_version\":\"%s\"},"
    "\"availability_topic\":\"" DANFOSS_ROOT "/availability\",%s}",
    name, kind, FIRMWARE, fields);
  return n >= 0 && size_t(n) < capacity;
}
}
