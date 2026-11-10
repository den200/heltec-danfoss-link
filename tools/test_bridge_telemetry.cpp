#define HOUSEHOLD_TEST  // tools/fakes/household_test.h
#include "../firmware-bridge/src/mqtt_entities.h"
#include <assert.h>
#include <string.h>
#include <initializer_list>
static size_t unhex(const char *s, uint8_t *out) {
  size_t n = strlen(s) / 2; unsigned value;
  for (size_t i = 0; i < n; ++i) { sscanf(s + 2*i, "%2x", &value); out[i] = value; }
  return n;
}
int main() {
  Bridge::Head h; h.node = 6;
  BridgeEntities::Clock clock; // unsynchronized: timestamps are null
  uint8_t frame[64]; char body[2048];
  size_t n = unhex("ffd2bb2d06410139019100021028140c054720202021030020102011030f031703280318032f0330076407a5075600b30287fe001ea7efd425", frame);
  h.observe(frame, n, 1000);
  assert(h.roomKnown && h.roomC100 == 1878 && h.statusRaw == 0xfe00);
  assert(Danfoss::batteryLow(h.statusRaw) == 1);
  assert(BridgeEntities::state(h, 1001, clock, body, sizeof body)); puts(body);
  assert(strstr(body, "\"last_seen\":null") && !strstr(body, "age"));
  assert(h.frontC100 == 1892 && h.valveC100 == 1957 && h.valvePermille == 647 && h.word46 == 0xb3);
  assert(strstr(body, "\"front_temperature\":18.92,\"valve_temperature\":19.57,\"valve_opening\":64.7,\"report_word46\":\"00b3\""));
  assert(strstr(body, "\"battery_level\":null")); // fe00 is the low flag, not a level
  n = unhex("ffd2bb2d06410135019100021024170c0546030020102011030f0317030103280318032f0330075d00b302a0fe0006a4e6aa923e09", frame);
  h.observe(frame, n, 900500); // a button wake must not freshen the older room sample
  assert(h.roomC100 == 1878 && h.roomSeenMs == 1000);
  assert(BridgeEntities::state(h, 902000, clock, body, sizeof body)); puts(body);
  frame[44] = 0x4f; frame[45] = 0;
  h.observe(frame, n, 903000);
  assert(Danfoss::batteryLow(h.statusRaw) == 0);
  frame[45] = 1; h.observe(frame, n, 904000);
  assert(Danfoss::batteryLow(h.statusRaw) == -1);
  assert(BridgeEntities::state(h, 904000, clock, body, sizeof body)); puts(body);
  // Healthy band 3400..5300 with a zero low byte; 3300 sits below every labelled
  // healthy observation and stays unknown.
  for (uint16_t healthy : {uint16_t(0x3400), uint16_t(0x4400), uint16_t(0x5300)}) assert(Danfoss::batteryLow(healthy) == 0);
  for (uint16_t unknown : {uint16_t(0x3300), uint16_t(0x5400), uint16_t(0x4401), uint16_t(0)}) assert(Danfoss::batteryLow(unknown) == -1);
  assert(Danfoss::batteryLevel(0x4400) == 68 && Danfoss::batteryLevel(0x2b00) == 43 && Danfoss::batteryLevel(0x6400) == 100);
  assert(Danfoss::batteryLevel(0xfe00) == -1 && Danfoss::batteryLevel(0x4401) == -1);
  // One fixed NTP base: republishing later yields byte-identical state.
  clock.baseEpoch = 1792652400; clock.baseMs = 500; // 2026-10-22T07:00:00Z at ms 500
  h.rssi10 = -615; h.replyContextKnown = true; h.replyContextRaw = 0x010203f0;
  char later[2048];
  assert(BridgeEntities::state(h, 904000, clock, body, sizeof body));
  assert(BridgeEntities::state(h, 914000, clock, later, sizeof later) && !strcmp(body, later));
  assert(strstr(body, "\"last_seen\":\"2026-10-22T07:15:03Z\"") && strstr(body, "\"room_updated\":\"2026-10-22T07:00:00Z\""));
  assert(strstr(body, "\"rssi\":-62") && strstr(body, "\"updates_left\":15"));
  char iso[32];
  BridgeEntities::isoTime(clock, 400, 904000, iso, sizeof iso); // just before the base
  assert(!strcmp(iso, "\"2026-10-22T07:00:00Z\""));
  // An instant from before a base refreshed 10 days later, seen 20 days after it:
  // the old int32 difference wrapped into the future here.
  BridgeEntities::Clock refreshed; refreshed.baseEpoch = 1792652400 + 864000; refreshed.baseMs = 500 + 864000000;
  BridgeEntities::isoTime(refreshed, 500, 500 + 1728000000u, iso, sizeof iso);
  assert(!strcmp(iso, "null")); // older than ~12 days: not published
  BridgeEntities::isoTime(refreshed, 500 + 700000000u, 500 + 1000000000u, iso, sizeof iso);
  assert(!strcmp(iso, "\"2026-10-30T09:26:40Z\"")); // 700000 s after the first base, before the refresh
  BridgeEntities::isoTime(clock, 0xfffff000u, 0x00001000u, iso, sizeof iso); // across the 49.7-day wrap
  assert(strcmp(iso, "null"));
  for (const char *kind : BridgeEntities::HEAD_KINDS) {
    assert(BridgeEntities::discovery(16, kind, body, sizeof body)); puts(body);
    assert(!BridgeEntities::discovery(16, kind, body, 20));
    assert(strstr(body, "\"name\":") && !strstr(body, "\"name\":\"A")); // device name not repeated
  }
  assert(BridgeEntities::discovery(8, "climate", body, sizeof body) && strstr(body, "{\"name\":null,"));
  // The app shows the requested value while a sleeping head waits for it.
  assert(strstr(body, "\"temperature_state_template\":\"{{ value_json.requested_target if value_json.requested_target is not none else value_json.accepted_target }}\""));
  assert(BridgeEntities::discovery(8, "room", body, sizeof body) &&
         strstr(body, "{\"name\":\"Room temperature estimate\",") && strstr(body, "\"device\":{\"identifiers\":[\"danfoss_ffd2bb2d_head_8\"],\"name\":\"Bedroom 3\""));
  // Only the climate entity carries the full JSON as attributes.
  assert(BridgeEntities::discovery(8, "rssi", body, sizeof body) && !strstr(body, "json_attributes_topic"));
  assert(!BridgeEntities::discovery(8, "bogus", body, sizeof body));
  for (const char *kind : BridgeEntities::BRIDGE_KINDS) {
    assert(!BridgeEntities::bridgeDiscovery(kind, body, 40));
    assert(BridgeEntities::bridgeDiscovery(kind, body, sizeof body)); puts(body);
    assert(strstr(body, "\"identifiers\":[\"danfoss_ffd2bb2d\"]") && strstr(body, BridgeEntities::FIRMWARE));
  }
  assert(!BridgeEntities::bridgeDiscovery("bogus", body, sizeof body));
  assert(!strcmp(BridgeEntities::domain("restart"), "button") && !strcmp(BridgeEntities::domain("autostart"), "binary_sensor"));
}
