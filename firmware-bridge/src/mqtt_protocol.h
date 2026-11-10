#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "household.h"
namespace BridgeMqtt {
static constexpr uint8_t BENCH_NODE = HOUSEHOLD_BENCH_NODE;
static constexpr const char *ROOT = DANFOSS_ROOT;
static constexpr const char *SET_TOPIC = DANFOSS_ROOT "/head/" HOUSEHOLD_STR(HOUSEHOLD_BENCH_NODE) "/set";
static constexpr const char *SET_FILTER = DANFOSS_ROOT "/head/+/set";
inline bool controllable(uint8_t node) {
  return Household::known(node);
}
inline bool commandNode(const char *topic, size_t n, uint8_t &node) {
  const char prefix[] = DANFOSS_ROOT "/head/";
  const size_t start = sizeof(prefix) - 1;
  if (!topic || n < start + 5 || memcmp(topic, prefix, start)) return false;
  size_t i = start; unsigned value = 0;
  if (topic[i] == '0') return false;
  while (i < n && topic[i] >= '0' && topic[i] <= '9') {
    value = value * 10 + unsigned(topic[i++] - '0');
    if (value > 232) return false;
  }
  if (n - i != 4 || memcmp(topic + i, "/set", 4) || !controllable(value)) return false;
  node = value; return true;
}

// HA sends plain Celsius. No exponents, extra tokens, embedded NUL or rounding
// to a different requested value. Accept half-degree steps within the head range.
inline bool temperature(const char *p, size_t n, uint16_t &out) {
  if (!p || !n || n > 5) return false;
  size_t i = 0; unsigned whole = 0, digits = 0;
  while (i < n && p[i] >= '0' && p[i] <= '9') {
    whole = whole * 10 + unsigned(p[i++] - '0');
    if (++digits > 2) return false;
  }
  if (!digits) return false;
  unsigned fraction = 0;
  if (i < n) {
    if (p[i++] != '.' || i == n) return false;
    char first = p[i++];
    if (first != '0' && first != '5') return false;
    fraction = first == '5' ? 50 : 0;
    if (i < n && p[i] == '0') ++i;
  }
  unsigned value = whole * 100 + fraction;
  if (i != n || value < 400 || value > 2800) return false;
  out = value;
  return true;
}
// A local (button) action at or after the command's arrival wins. Ages are taken
// from now, so a button press weeks ago never blocks commands after millis() wraps.
inline bool supersededByLocal(bool seen, uint32_t localMs, uint32_t commandMs, uint32_t now) {
  return seen && now - localMs <= now - commandMs;
}
inline bool command(const char *topic, size_t topicLen, const char *data, size_t len,
                    bool retained, bool duplicate, size_t offset, size_t total,
                    uint16_t &out) {
  uint8_t node;
  return !retained && !duplicate && !offset && total == len && topic &&
         commandNode(topic, topicLen, node) &&
         temperature(data, len, out);
}
}
