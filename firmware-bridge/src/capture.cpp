// Receive-only capture firmware for finding a Danfoss Link network's Home ID and
// network key (docs/GETTING-STARTED.md). Needs no key, no Home ID and no Wi-Fi.
// Never transmits.
//
// Syncs on the generic start of frame (inverted preamble 0x55 -> AA, SOF F0 -> 0F),
// so it hears every Link network in range. Each checksum-valid frame is printed on
// USB serial (115200) as one JSON line with its full hex. Pairing frames (op 80
// sub 04) carry the network key in clear: treat a capture log as a secret and feed
// it to tools/extract_key.py, which stores the key in the macOS Keychain.

#include <Arduino.h>
#include <RadioLib.h>

#define P_NSS 8
#define P_DIO1 14
#define P_RST 12
#define P_BUSY 13
#define P_SCK 9
#define P_MISO 11
#define P_MOSI 10
#define TCXO_V 1.8

static const float RF_FREQ = 868.40, RF_BR = 40.0, RF_DEV = 20.0, RF_RXBW = 117.3;
static const uint8_t SYNC[2] = {0xaa, 0x0f};
static const size_t WINDOW = 57;  // Home ID onwards; longest frame the bridge accepts

SPIClass spi(FSPI);
SX1262 radio = new Module(P_NSS, P_DIO1, P_RST, P_BUSY, spi);
volatile bool received = false;
uint32_t valid = 0, rejected = 0, lastSummary = 0;

void IRAM_ATTR onRx() { received = true; }

static uint8_t checksum(const uint8_t *f, size_t n) {
  uint8_t c = 0xff;
  for (size_t i = 0; i < n; i++) c ^= f[i];
  return c;
}

static void listen() {
  radio.standby();
  radio.setSyncWord((uint8_t *)SYNC, sizeof SYNC);
  radio.fixedPacketLengthMode(WINDOW);
  radio.setPacketReceivedAction(onRx);
  radio.startReceive();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  spi.begin(P_SCK, P_MISO, P_MOSI, P_NSS);
  int st = radio.beginFSK(RF_FREQ, RF_BR, RF_DEV, RF_RXBW, 0, 80, TCXO_V);
  if (st == RADIOLIB_ERR_NONE) {
    radio.setDio2AsRfSwitch(true);
    radio.setRxBoostedGainMode(true);
    radio.setCRC(0);
    radio.setWhitening(false);
    radio.setDataShaping(RADIOLIB_SHAPING_NONE);
    listen();
  }
  Serial.printf("{\"t\":\"capture_boot\",\"freq\":%.2f,\"radio\":%d}\n", RF_FREQ, st);
}

void loop() {
  if (received) {
    received = false;
    uint8_t raw[WINDOW], f[WINDOW];
    int st = radio.readData(raw, WINDOW);
    float rssi = radio.getRSSI();
    for (size_t i = 0; i < WINDOW; i++) f[i] = raw[i] ^ 0xff;
    size_t n = f[7];
    if (st == RADIOLIB_ERR_NONE && n >= 10 && n <= WINDOW && checksum(f, n - 1) == f[n - 1]) {
      valid++;
      Serial.printf("{\"t\":\"frame\",\"ms\":%lu,\"rssi\":%.1f,\"home\":\"%02x%02x%02x%02x\",\"src\":%u,\"dst\":%u,\"len\":%u,\"hex\":\"",
                    (unsigned long)millis(), rssi, f[0], f[1], f[2], f[3], f[4], f[8], (unsigned)n);
      for (size_t i = 0; i < n; i++) Serial.printf("%02x", f[i]);
      Serial.println("\"}");
    } else {
      rejected++;  // false sync matches and noise
    }
    listen();
  }
  if (millis() - lastSummary >= 60000) {
    lastSummary = millis();
    Serial.printf("{\"t\":\"capture_summary\",\"frames\":%lu,\"rejected\":%lu}\n",
                  (unsigned long)valid, (unsigned long)rejected);
  }
  delay(1);
}
