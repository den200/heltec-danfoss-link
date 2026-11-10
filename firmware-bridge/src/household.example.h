#pragma once
// Copy to household.private.h (gitignored) and replace every value.
// docs/GETTING-STARTED.md: tools/extract_key.py prints your Home ID and the node
// numbers it heard; read a head's node number off the capture while you turn it.

// The 4-byte Home ID of your Link network, 8 lowercase hex digits. It is sent in
// clear at the start of every frame and becomes part of the MQTT topics and the
// Home Assistant unique IDs, so changing it later creates new HA entities.
#define HOUSEHOLD_HOME_ID "00000000"

// X(node, "room name") for every head, in ascending node order. Two heads may share
// a room; each still regulates on its own sensor.
#define HOUSEHOLD_HEADS(X) \
  X(2, "Living room") \
  X(3, "Living room") \
  X(4, "Bedroom") \
  X(5, "Kitchen")

// Optional: nodes deleted from the network whose old HA entities should be removed.
// #define HOUSEHOLD_RETIRED_NODES(X) X(7)

// Optional: a spare head on the bench used for protocol trials (enables IMPORT_CC).
// #define HOUSEHOLD_BENCH_NODE 9
