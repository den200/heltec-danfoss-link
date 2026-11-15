#pragma once
// Wi-Fi/MQTT recovery decisions, kept free of Arduino calls so the host tests can
// drive them. Arduino's auto-reconnect gives up for good after some disconnect
// reasons (e.g. AUTH_FAIL or ASSOC_LEAVE while an access point reboots); on
// 15 Nov 2026 that left the bridge offline for 14 h after a 4-minute Wi-Fi outage.
#include <stdint.h>

namespace BridgeWifi {
static constexpr uint32_t FIRST_RETRY_MS = 30000;          // Wi-Fi down this long: begin() again
static constexpr uint32_t MAX_RETRY_MS = 300000;           // backoff ceiling
static constexpr uint32_t WIFI_RESTART_MS = 15UL * 60000;  // still no Wi-Fi: restart the board
static constexpr uint32_t MQTT_RESTART_MS = 60UL * 60000;  // Wi-Fi up but no broker (HA may be updating)
static constexpr uint32_t RESTART_REPEAT_MS = 60000;       // a refused restart is asked for again

enum Action : uint8_t { NONE, REBEGIN, RESTART };

struct Supervisor {
  bool wifiDown = false, mqttDown = false, restartAsked = false;
  uint32_t wifiDownSince = 0, lastBegin = 0, retryMs = FIRST_RETRY_MS;
  uint32_t mqttDownSince = 0, lastRestartAsk = 0;

  // Call often (every loop pass) with millis(). Wrap-safe: only differences are used.
  Action tick(bool wifiUp, bool mqttUp, uint32_t now) {
    if (wifiUp) { wifiDown = false; retryMs = FIRST_RETRY_MS; }
    else if (!wifiDown) { wifiDown = true; wifiDownSince = lastBegin = now; }
    if (mqttUp) { mqttDown = false; restartAsked = false; }
    else if (!mqttDown) { mqttDown = true; mqttDownSince = now; }

    bool stuck = (wifiDown && now - wifiDownSince >= WIFI_RESTART_MS) ||
                 (mqttDown && now - mqttDownSince >= MQTT_RESTART_MS);
    if (stuck && (!restartAsked || now - lastRestartAsk >= RESTART_REPEAT_MS)) {
      restartAsked = true; lastRestartAsk = now;
      return RESTART;
    }
    if (wifiDown && now - lastBegin >= retryMs) {
      lastBegin = now;
      retryMs = retryMs * 2 > MAX_RETRY_MS ? MAX_RETRY_MS : retryMs * 2;
      return REBEGIN;
    }
    return NONE;
  }
};
}
