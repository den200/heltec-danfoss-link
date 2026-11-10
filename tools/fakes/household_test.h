#pragma once
// Household used by the host tests (compiled with HOUSEHOLD_TEST). The Home ID is
// the one in the captured frames under data/; the room names are placeholders.
#define HOUSEHOLD_HOME_ID "ffd2bb2d"

#define HOUSEHOLD_HEADS(X) \
  X(2, "Living room") \
  X(5, "Small bedroom") \
  X(6, "Living room") \
  X(8, "Bedroom 3") \
  X(12, "Master bedroom") \
  X(13, "Guest room") \
  X(16, "A")

#define HOUSEHOLD_RETIRED_NODES(X) X(15)
#define HOUSEHOLD_BENCH_NODE 16
