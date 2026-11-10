// Drone Remote ID receiver (ASTM F3411 / ASD-STAN EN 4709-002) using the
// ESP32-S3's own 2.4 GHz radio: WiFi beacons and NAN action frames in
// promiscuous mode, plus BLE legacy adverts (service data UUID 0xFFFA).
//
// Radio callbacks only decode into a per-drone slot; ridPoll() prints from
// loop() so serial output never comes from two tasks at once. The operator's
// ID and position are not reported: Q7 asks how many drones fly, not who flies them.

#include "rid.h"

#include <NimBLEDevice.h>
#include <esp_wifi.h>

extern "C" {
#include "opendroneid.h"
}

#define SLOTS 8
#define EMIT_EVERY_MS 2000
#define SLOT_EXPIRE_MS 60000

struct Slot {
  uint8_t mac[6];
  bool used, dirty;
  char src;  // 'w' wifi, 'b' ble
  int8_t rssi;
  uint8_t chan;
  unsigned long seen, emitted;
  ODID_UAS_Data uas;
};

static Slot slots[SLOTS];
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static ODID_UAS_Data scratch;  // only touched from the WiFi task
static volatile uint32_t wifiFrames = 0, bleAdverts = 0, ridMessages = 0;
static uint8_t channel = 6;
static unsigned long hopAt = 0;

// Caller holds mux.
static Slot *slotFor(const uint8_t *mac, char src) {
  Slot *free = nullptr, *oldest = &slots[0];
  for (auto &s : slots) {
    if (s.used && s.src == src && memcmp(s.mac, mac, 6) == 0) return &s;
    if (!s.used && !free) free = &s;
    if (s.seen < oldest->seen) oldest = &s;
  }
  Slot *s = free ? free : oldest;
  memset(s, 0, sizeof(*s));
  memcpy(s->mac, mac, 6);
  s->src = src;
  s->used = true;
  odid_initUasData(&s->uas);
  return s;
}

// Merges a decoded WiFi pack into the drone's slot: keep any field it carried.
static void mergeInto(ODID_UAS_Data *dst, const ODID_UAS_Data *src) {
  for (int i = 0; i < ODID_BASIC_ID_MAX_MESSAGES; i++)
    if (src->BasicIDValid[i]) { dst->BasicID[i] = src->BasicID[i]; dst->BasicIDValid[i] = 1; }
  if (src->LocationValid) { dst->Location = src->Location; dst->LocationValid = 1; }
  if (src->SystemValid) { dst->System = src->System; dst->SystemValid = 1; }
}

static void store(const uint8_t *mac, char src, int8_t rssi, const ODID_UAS_Data *pack,
                  const uint8_t *single) {
  portENTER_CRITICAL(&mux);
  Slot *s = slotFor(mac, src);
  if (pack) mergeInto(&s->uas, pack);
  if (single) decodeOpenDroneID(&s->uas, single);
  s->rssi = rssi;
  s->chan = src == 'w' ? channel : 0;
  s->seen = millis();
  s->dirty = true;
  ridMessages++;
  portEXIT_CRITICAL(&mux);
}

// Beacon: walk the tagged parameters looking for vendor IE FA:0B:BC type 0x0D.
static void onBeacon(const uint8_t *f, int len, int8_t rssi) {
  int i = 24 + 12;  // MAC header + fixed beacon fields
  while (i + 2 <= len) {
    uint8_t id = f[i], ielen = f[i + 1];
    const uint8_t *ie = f + i + 2;
    if (i + 2 + ielen > len) return;
    if (id == 0xDD && ielen > 5 && ie[0] == 0xFA && ie[1] == 0x0B && ie[2] == 0xBC && ie[3] == 0x0D) {
      // ie[4] is the message counter, the message pack follows
      if (odid_message_process_pack(&scratch, ie + 5, ielen - 5) > 0) store(f + 10, 'w', rssi, &scratch, nullptr);
      return;
    }
    i += 2 + ielen;
  }
}

static void onWifi(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  auto *p = (wifi_promiscuous_pkt_t *)buf;
  const uint8_t *f = p->payload;
  int len = p->rx_ctrl.sig_len - 4;  // drop FCS
  wifiFrames++;
  if (len < 40) return;
  if (f[0] == 0x80) {
    onBeacon(f, len, p->rx_ctrl.rssi);
  } else if (f[0] == 0xD0) {
    char mac[6];
    if (odid_wifi_receive_message_pack_nan_action_frame(&scratch, mac, f, len) == 0)
      store((uint8_t *)mac, 'w', p->rx_ctrl.rssi, &scratch, nullptr);
  }
}

class BleCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *d) override {
    bleAdverts++;
    std::string sd = d->getServiceData(NimBLEUUID((uint16_t)0xFFFA));
    // [0] app code 0x0D, [1] counter, [2..26] one 25-byte ODID message
    if (sd.size() < 2 + ODID_MESSAGE_SIZE || (uint8_t)sd[0] != 0x0D) return;
    uint8_t mac[6];
    memcpy(mac, d->getAddress().getNative(), 6);
    store(mac, 'b', d->getRSSI(), nullptr, (const uint8_t *)sd.data() + 2);
  }
};

void ridBegin() {
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_start();
  wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(onWifi);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new BleCb(), true);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(30);  // BLE shares the 2.4 GHz radio with WiFi; 90 % starved WiFi to ~2 frames/s
  scan->setMaxResults(0);
  scan->start(0, nullptr, false);
}

static void printFloat(const char *key, double v, int prec) {
  Serial.printf(",\"%s\":%.*f", key, prec, v);
}

void ridPoll() {
  unsigned long now = millis();
  // NAN Remote ID lives on channel 6; beacons can be anywhere, so visit 1 and 11 briefly.
  if (now >= hopAt) {
    channel = channel == 6 ? 1 : channel == 1 ? 11 : 6;
    hopAt = now + (channel == 6 ? 4000 : 500);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  }

  for (auto &s : slots) {
    Slot c;
    portENTER_CRITICAL(&mux);
    bool emit = s.used && s.dirty && now - s.emitted >= EMIT_EVERY_MS;
    if (s.used && now - s.seen > SLOT_EXPIRE_MS) s.used = false;
    if (emit) {
      c = s;
      s.dirty = false;
      s.emitted = now;
    }
    portEXIT_CRITICAL(&mux);
    if (!emit) continue;

    Serial.printf("{\"t\":\"rid\",\"ms\":%lu,\"src\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"rssi\":%d",
                  now, c.src == 'w' ? "wifi" : "ble", c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5], c.rssi);
    if (c.chan) Serial.printf(",\"chan\":%u", c.chan);
    if (c.uas.BasicIDValid[0])
      Serial.printf(",\"id\":\"%s\",\"id_type\":%d,\"ua_type\":%d", c.uas.BasicID[0].UASID,
                    c.uas.BasicID[0].IDType, c.uas.BasicID[0].UAType);
    const ODID_Location_data &l = c.uas.Location;
    if (c.uas.LocationValid && (l.Latitude != 0 || l.Longitude != 0)) {
      printFloat("lat", l.Latitude, 6);
      printFloat("lon", l.Longitude, 6);
      if (l.AltitudeGeo > -1000) printFloat("alt", l.AltitudeGeo, 1);
      if (l.Height > -1000) printFloat("height", l.Height, 1);
      if (l.SpeedHorizontal < 255) printFloat("speed", l.SpeedHorizontal, 1);
      if (l.Direction <= 360) printFloat("hdg", l.Direction, 0);
      Serial.printf(",\"status\":%d", l.Status);
    }
    Serial.print("}\n");
  }
}

void ridStats(uint32_t &frames, uint32_t &adverts, uint32_t &messages) {
  frames = wifiFrames;
  adverts = bleAdverts;
  messages = ridMessages;
}
