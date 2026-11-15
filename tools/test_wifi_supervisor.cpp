#include "../firmware-bridge/src/wifi_supervisor.h"
#include <assert.h>
#include <stdio.h>
using namespace BridgeWifi;

// Run the supervisor from `from` to `to` in 1 s steps; count actions.
static void run(Supervisor &s, bool wifi, bool mqtt, uint32_t from, uint32_t to, int &begins, int &restarts) {
  for (uint32_t t = from; t != to; t += 1000) {
    Action a = s.tick(wifi, mqtt, t);
    begins += a == REBEGIN; restarts += a == RESTART;
  }
}

int main() {
  int b = 0, r = 0;
  { // Healthy: nothing happens.
    Supervisor s; run(s, true, true, 0, 3600000, b, r); assert(b == 0 && r == 0);
  }
  { // Short outage (the other devices on 15 Nov): no action before 30 s, then begin().
    Supervisor s; b = r = 0;
    run(s, true, true, 0, 10000, b, r);
    run(s, false, false, 10000, 39000, b, r); assert(b == 0 && r == 0);
    run(s, false, false, 39000, 41000, b, r); assert(b == 1 && r == 0);
    run(s, true, true, 41000, 100000, b, r); assert(b == 1 && r == 0);
    // Next outage starts again at 30 s, not at the grown backoff.
    run(s, false, false, 100000, 131000, b, r); assert(b == 2 && r == 0);
  }
  { // Stuck Wi-Fi: retries at 30, 90, 210, 450, 750 s (backoff, capped at 300 s), restart at 15 min.
    Supervisor s; b = r = 0;
    run(s, false, false, 0, 15 * 60000, b, r); assert(b == 5 && r == 0);
    assert(s.tick(false, false, 15 * 60000) == RESTART);
    // A refused restart is asked for again after a minute, not on every pass.
    b = r = 0; run(s, false, false, 15 * 60000 + 1000, 16 * 60000, b, r); assert(r == 0);
    assert(s.tick(false, false, 16 * 60000) == RESTART);
  }
  { // Wi-Fi up but broker gone (HA updating): no begin(), restart only after 60 min.
    Supervisor s; b = r = 0;
    run(s, true, false, 0, 60 * 60000, b, r); assert(b == 0 && r == 0);
    assert(s.tick(true, false, 60 * 60000) == RESTART);
  }
  { // Broker back before the limit resets the clock.
    Supervisor s; b = r = 0;
    run(s, true, false, 0, 50 * 60000, b, r);
    s.tick(true, true, 50 * 60000);
    run(s, true, false, 50 * 60000 + 1000, 100 * 60000, b, r); assert(r == 0);
  }
  { // millis() wrap during an outage.
    Supervisor s; b = r = 0;
    uint32_t start = 0xFFFFFFFFu - 100000;
    run(s, false, false, start, start + 15 * 60000, b, r); assert(b == 5 && r == 0);
    assert(s.tick(false, false, start + 15 * 60000) == RESTART);
  }
  puts("wifi_supervisor ok");
}
