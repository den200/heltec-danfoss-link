#pragma once
// Password-protected firmware updates over Wi-Fi (espota, port 3232), with a
// software fallback because the stock bootloader has no app rollback: a newly
// written image is on trial until it reaches the MQTT broker. Three boots or
// five minutes without that, and the previous image is booted again. The
// trial record lives in its own NVS namespace, apart from the V3 checkpoint.
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

namespace BridgeOta {
static constexpr uint32_t TRIAL_MS = 300000;
static constexpr uint8_t TRIAL_BOOTS = 3;
static bool trial = false, started = false;
static char trialFrom[17] = "";

static void clearRecord() {
  Preferences p;
  p.begin("ota", false);
  p.clear();
  p.end();
}

static void revert(const char *why) {
  const esp_partition_t *previous =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, trialFrom);
  bool ok = previous && esp_ota_set_boot_partition(previous) == ESP_OK; // validates the image
  Serial.printf("{\"t\":\"%s\",\"why\":\"%s\",\"to\":\"%s\"}\n",
                ok ? "ota_revert" : "ota_revert_failed", why, trialFrom);
  // Either way the trial is over: never retry a failed revert in a loop.
  clearRecord();
  trial = false;
  if (ok) {
    delay(200);
    ESP.restart();
  }
}

// SHA-256 of an app partition's image, as hex; empty if unreadable.
static String imageHash(const esp_partition_t *part) {
  uint8_t sha[32];
  if (!part || esp_partition_get_sha256(part, sha) != ESP_OK) return "";
  char hex[65];
  for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", sha[i]);
  return String(hex);
}

// Early in setup(), before anything that could crash on a bad image.
inline void bootCheck() {
  Preferences p;
  p.begin("ota", false);
  String pending = p.getString("pending", "");
  const esp_partition_t *running = esp_ota_get_running_partition();
  // Same slot AND same image: a USB flash into that slot meanwhile is not on trial.
  if (pending.length() && running && pending == running->label && p.getString("sha", "") == imageHash(running)) {
    uint8_t boots = p.getUChar("boots", 0) + 1;
    p.putUChar("boots", boots);
    snprintf(trialFrom, sizeof trialFrom, "%s", p.getString("from", "").c_str());
    p.end();
    trial = true;
    if (boots > TRIAL_BOOTS) revert("boot loop");
    return;
  }
  if (pending.length()) p.clear(); // the update never booted; nothing to confirm
  p.end();
}

// Network task, once Wi-Fi is up. Empty password disables updates.
inline void begin(const char *password) {
  if (started || !password[0]) return;
  ArduinoOTA.setHostname("danfoss-bridge");
  ArduinoOTA.setPassword(password);
  ArduinoOTA.setRebootOnSuccess(false);
  ArduinoOTA.onEnd([]() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_boot_partition(); // set by the finished update
    Preferences p;
    p.begin("ota", false);
    p.putString("pending", next ? next->label : "");
    p.putString("sha", imageHash(next));
    p.putString("from", running ? running->label : "");
    p.putUChar("boots", 0);
    p.end();
    Serial.println("{\"t\":\"ota_written\"}");
    delay(300);
    ESP.restart();
  });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("{\"t\":\"err\",\"where\":\"ota\",\"why\":\"update failed\",\"code\":%d}\n", int(e)); });
  ArduinoOTA.begin();
  started = true;
}

// Network task loop. Returns true once when a trial image is confirmed.
inline bool tick(bool mqttConnected) {
  if (started) ArduinoOTA.handle();
  if (!trial) return false;
  if (mqttConnected) {
    Preferences p;
    p.begin("ota", false);
    p.clear();
    p.end();
    trial = false;
    return true;
  }
  if (millis() > TRIAL_MS) revert("no MQTT within 5 minutes");
  return false;
}
inline const char *runningLabel() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  return running ? running->label : "?";
}
}
