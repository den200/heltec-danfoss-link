#pragma once
// Per-household settings: the Link network's Home ID and the heads with their room
// names. Copy household.example.h to household.private.h (gitignored) and fill it
// in; docs/GETTING-STARTED.md shows how to find the Home ID and node numbers.
#include <stdint.h>

#if defined(HOUSEHOLD_TEST)
#include "household_test.h"  // tools/fakes: fixed values for the host tests
#elif __has_include("household.private.h")
#include "household.private.h"
#else
#error "Missing firmware-bridge/src/household.private.h: copy household.example.h and fill it in (docs/GETTING-STARTED.md)"
#endif

#ifndef HOUSEHOLD_RETIRED_NODES
#define HOUSEHOLD_RETIRED_NODES(X)
#endif
#ifndef HOUSEHOLD_BENCH_NODE
#define HOUSEHOLD_BENCH_NODE 0
#endif

#define HOUSEHOLD_STR_(x) #x
#define HOUSEHOLD_STR(x) HOUSEHOLD_STR_(x)
// MQTT topic root and Home Assistant unique-ID prefix, as string literals.
#define DANFOSS_ROOT "danfoss/" HOUSEHOLD_HOME_ID
#define DANFOSS_UID "danfoss_" HOUSEHOLD_HOME_ID

namespace Household {
constexpr int nibble(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
constexpr bool validHex(const char *s, int i) {
  return i == 8 ? s[8] == 0 : nibble(s[i]) >= 0 && validHex(s, i + 1);
}
static_assert(validHex(HOUSEHOLD_HOME_ID, 0), "HOUSEHOLD_HOME_ID must be 8 lowercase hex digits");
// Byte i (0..3) of the Home ID, as transmitted.
constexpr uint8_t homeByte(int i) {
  return uint8_t(nibble(HOUSEHOLD_HOME_ID[2 * i]) << 4 | nibble(HOUSEHOLD_HOME_ID[2 * i + 1]));
}

#define HOUSEHOLD_IS_NODE_(n, room) node == n ||
// A head the bridge listens to and may control.
inline bool known(uint8_t node) { return HOUSEHOLD_HEADS(HOUSEHOLD_IS_NODE_) false; }
#define HOUSEHOLD_IS_RETIRED_(n) node == n ||
// A node removed from the Link network: no HA entities, retained discovery cleared.
inline bool retired(uint8_t node) { return HOUSEHOLD_RETIRED_NODES(HOUSEHOLD_IS_RETIRED_) false; }
}  // namespace Household
