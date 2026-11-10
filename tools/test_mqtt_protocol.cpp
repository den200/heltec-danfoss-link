#define HOUSEHOLD_TEST  // tools/fakes/household_test.h
#include "../firmware-bridge/src/mqtt_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>
int main() {
  uint16_t target = 999;
  for (const char *s : {"4", "4.0", "4.00", "04.00"}) {
    assert(BridgeMqtt::temperature(s, strlen(s), target) && target == 400);
  }
  assert(BridgeMqtt::temperature("17.5", 4, target) && target == 1750);
  assert(BridgeMqtt::temperature("28.00", 5, target) && target == 2800);
  for (const char *s : {"", "NaN", "inf", "-1", "3.5", "28.5", "99", "4e1", "17.25", "17.51", "17.", " 17", "17 ", "17\n", "17.500", "17x", "017"}) {
    target = 999;
    assert(!BridgeMqtt::temperature(s, strlen(s), target) && target == 999);
  }
  assert(!BridgeMqtt::temperature("17\0", 3, target));
  assert(BridgeMqtt::supersededByLocal(true, 101, 100, 200));
  assert(BridgeMqtt::supersededByLocal(true, 100, 100, 200));
  assert(!BridgeMqtt::supersededByLocal(true, 100, 101, 200));
  assert(!BridgeMqtt::supersededByLocal(false, 101, 100, 200));
  assert(!BridgeMqtt::supersededByLocal(true, 0xfffffff0, 10, 20));
  assert(BridgeMqtt::supersededByLocal(true, 10, 0xfffffff0, 20));
  // A button press 30 days ago must not block a command now (was rejected for 24.8 days).
  const uint32_t day = 86400000u;
  for (uint32_t ago : {25 * day, 30 * day, 40 * day, 49 * day})
    assert(!BridgeMqtt::supersededByLocal(true, 1000, 1000 + ago - 100, 1000 + ago));
  const char *topic = BridgeMqtt::SET_TOPIC;
  assert(BridgeMqtt::command(topic, strlen(topic), "18", 2, false, false, 0, 2, target));
  assert(!BridgeMqtt::command(topic, strlen(topic), "18", 2, true, false, 0, 2, target));
  assert(!BridgeMqtt::command(topic, strlen(topic), "18", 2, false, true, 0, 2, target));
  assert(!BridgeMqtt::command(topic, strlen(topic), "18", 2, false, false, 1, 3, target));
  assert(!BridgeMqtt::command(topic, strlen(topic), "18", 2, false, false, 0, 3, target));
  const char *other = "danfoss/ffd2bb2d/head/15/set";
  assert(!BridgeMqtt::command(other, strlen(other), "18", 2, false, false, 0, 2, target));
  for (const char *s : {"danfoss/ffd2bb2d/head/2/set", "danfoss/ffd2bb2d/head/6/set",
       "danfoss/ffd2bb2d/head/8/set", "danfoss/ffd2bb2d/head/12/set", "danfoss/ffd2bb2d/head/13/set"}) {
    uint8_t node = 0;
    assert(BridgeMqtt::commandNode(s, strlen(s), node) && BridgeMqtt::controllable(node));
    assert(BridgeMqtt::command(s, strlen(s), "18", 2, false, false, 0, 2, target));
  }
  for (const char *s : {"danfoss/ffd2bb2d/head/15/set", "danfoss/ffd2bb2d/head/016/set",
       "danfoss/ffd2bb2d/head/256/set", "danfoss/ffd2bb2d/head/16/set/x", "danfoss/ffd2bb2d/head/16x/set"}) {
    uint8_t node = 99;
    assert(!BridgeMqtt::commandNode(s, strlen(s), node) && node == 99);
  }
  puts("MQTT temperature validation and local precedence passed");
}
