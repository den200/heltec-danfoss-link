// listening-post firmware v0.5: receive-only listener for the Heltec V3.
//
// Listens on the MeshCore DK preset and, once a minute, sweeps RSSI across
// 863–870 MHz. The ESP32-S3's own 2.4 GHz radio listens for drone Remote ID
// (see rid.cpp). Every event goes out on USB serial as one JSON object per line;
// Temporary test build: explicit DANFOSS_TX permits only a 28 C command to node 16.

#include <Arduino.h>
#include <RadioLib.h>
#include <SSD1306Wire.h>

#include "rid.h"
#include "danfoss_protocol.h"
#include "danfoss_key.h"

// Heltec V3 pins
#define P_NSS 8
#define P_DIO1 14
#define P_RST 12
#define P_BUSY 13
#define P_SCK 9
#define P_MISO 11
#define P_MOSI 10
#define P_VEXT 36
#define P_OLED_RST 21
#define TCXO_V 1.8

// MeshCore DK preset
#define MESH_FREQ 869.618
#define MESH_BW 62.5
#define MESH_SF 8
#define MESH_CR 8
#define MESH_SYNC 0x12
#define MESH_PREAMBLE 16

// Sweep
#define SWEEP_START 863.0
#define SWEEP_STOP 870.0
#define SWEEP_STEP 0.1
#define SWEEP_PERIOD_MS 60000UL
#define SWEEP_DWELL_US 2000

SPIClass spi(FSPI);
SX1262 radio = new Module(P_NSS, P_DIO1, P_RST, P_BUSY, spi);
SSD1306Wire oled(0x3c, 17, 18);

struct LoraCfg {
  float freq, bw;
  int sf, cr, sync, pre;
  bool crc, iq;
  int ldro;  // -1 auto, 0 off, 1 on
};
const LoraCfg meshCfg = {MESH_FREQ, MESH_BW, MESH_SF, MESH_CR, MESH_SYNC, MESH_PREAMBLE, true, false, -1};

// A listening window: the Pi asks for a different LoRa config for a while
// (e.g. a satellite pass), then the radio falls back to the mesh preset.
struct FskCfg {
  float freq, br, dev, rxbw;
  uint8_t sync[8];
  int syncLen, len;
};

struct Window {
  bool on, fsk;
  LoraCfg cfg;
  FskCfg fcfg;
  char tag[24];
  unsigned long end;
  uint32_t packets, preambles, headers;
} win = {};

volatile bool rxFlag = false;
uint32_t meshPackets = 0, crcErrors = 0, sweeps = 0;
float lastRssi = 0, lastSnr = 0, lastFloor = 0;
unsigned long lastSweep = 0;

void IRAM_ATTR onRx() { rxFlag = true; }

void hex(const uint8_t *b, size_t n) {
  static const char *d = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    Serial.write(d[b[i] >> 4]);
    Serial.write(d[b[i] & 15]);
  }
}

void drawStatus() {
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, 0, "listening-post v0.5");
  oled.drawString(0, 14, "mesh pkts: " + String(meshPackets) + "  crc: " + String(crcErrors));
  oled.drawString(0, 26, "last: " + String(lastRssi, 0) + " dBm  " + String(lastSnr, 1) + " dB");
  oled.drawString(0, 38, "floor: " + String(lastFloor, 0) + " dBm");
  uint32_t wf, ba, rm;
  ridStats(wf, ba, rm);
  oled.drawString(0, 50, "rid: " + String(rm) + "  up " + String(millis() / 60000) + " min");
  oled.display();
}

// In a window, preamble and header detections are latched but don't wake us,
// so a signal too weak to decode is still counted.
void listen() {
  rxFlag = false;
  if (win.on)
    radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF,
                       RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
                       RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
  else
    radio.startReceive();
}

void applyLora(const LoraCfg &c) {
  radio.standby();
  radio.setFrequency(c.freq);
  radio.setBandwidth(c.bw);
  radio.setSpreadingFactor(c.sf);
  radio.setCodingRate(c.cr);
  radio.setSyncWord(c.sync);
  radio.setPreambleLength(c.pre);
  radio.setCRC(c.crc);
  radio.invertIQ(c.iq);
  if (c.ldro < 0) radio.autoLDRO();
  else radio.forceLDRO(c.ldro);
  listen();
}

// FSK listening uses fixed-length packets with no CRC or whitening: framing,
// line coding (e.g. wM-Bus 3-of-6) and CRCs are decoded on the Pi.
void applyFsk(const FskCfg &c) {
  radio.standby();
  int st = radio.beginFSK(c.freq, c.br, c.dev, c.rxbw, 2, 16, TCXO_V);
  radio.setDio2AsRfSwitch(true);
  radio.setRxBoostedGainMode(true);
  radio.setSyncWord((uint8_t *)c.sync, c.syncLen);
  radio.setCRC(0);
  radio.setWhitening(false);
  radio.setDataShaping(RADIOLIB_SHAPING_NONE);
  radio.fixedPacketLengthMode(c.len);
  radio.setPacketReceivedAction(onRx);
  if (st != RADIOLIB_ERR_NONE) Serial.printf("{\"t\":\"err\",\"where\":\"beginFSK\",\"code\":%d}\n", st);
  rxFlag = false;
  radio.startReceive();
}

bool startMesh() {
  int s = radio.begin(MESH_FREQ, MESH_BW, MESH_SF, MESH_CR, MESH_SYNC, 2, MESH_PREAMBLE, TCXO_V);
  if (s != RADIOLIB_ERR_NONE) {
    Serial.printf("{\"t\":\"err\",\"where\":\"begin\",\"code\":%d}\n", s);
    return false;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setRxBoostedGainMode(true);
  radio.setCRC(true);
  radio.setPacketReceivedAction(onRx);
  radio.startReceive();
  return true;
}

void handlePacket() {
  rxFlag = false;
  size_t len = radio.getPacketLength();
  uint8_t buf[256];
  int s = radio.readData(buf, len);
  bool fsk = win.on && win.fsk;
  float rssi = radio.getRSSI(), snr = fsk ? 0 : radio.getSNR(), ferr = fsk ? 0 : radio.getFrequencyError();
  if (!win.on) {
    if (s == RADIOLIB_ERR_CRC_MISMATCH) crcErrors++;
    else if (s == RADIOLIB_ERR_NONE) meshPackets++;
  }
  lastRssi = rssi;
  lastSnr = snr;
  if (win.on && win.fsk) {
    win.packets++;
    Serial.printf("{\"t\":\"fsk\",\"tag\":\"%s\",\"freq\":%.4f,", win.tag, win.fcfg.freq);
  } else if (win.on) {
    win.packets++;
    Serial.printf("{\"t\":\"sat\",\"tag\":\"%s\",\"freq\":%.4f,", win.tag, win.cfg.freq);
  } else {
    Serial.print("{\"t\":\"mesh\",");
  }
  Serial.printf("\"ms\":%lu,\"ok\":%s,\"rssi\":%.1f,\"snr\":%.2f,\"ferr\":%.0f,\"len\":%u,\"hex\":\"",
                millis(), s == RADIOLIB_ERR_NONE ? "true" : "false", rssi, snr, ferr, (unsigned)len);
  hex(buf, len);
  Serial.print("\"}\n");
  listen();
}

// RSSI sweep: retune in LoRa RX mode and read the instantaneous channel RSSI.
// Returns the lowest reading. The caller restores the receive configuration.
float sweepRange(const char *type, float start, float stop, float step) {
  radio.standby();
  radio.setBandwidth(125.0);
  Serial.printf("{\"t\":\"%s\",\"ms\":%lu,\"start\":%.3f,\"step\":%.3f,\"rssi\":[", type, millis(), start, step);
  float floor = 0;
  int n = 0;
  for (float f = start; f <= stop + 0.0001; f += step, n++) {
    radio.setFrequency(f);
    radio.startReceive();
    delayMicroseconds(SWEEP_DWELL_US);
    float r = radio.getRSSI(false);
    if (n == 0 || r < floor) floor = r;
    Serial.printf(n ? ",%.1f" : "%.1f", r);
    radio.standby();
  }
  Serial.print("]}\n");
  return floor;
}

void sweep() {
  lastFloor = sweepRange("sweep", SWEEP_START, SWEEP_STOP, SWEEP_STEP);
  sweeps++;
  applyLora(meshCfg);
}

// A report is 57 bytes. Match Home ID plus source node in hardware so RX
// completes at the real checksum, rather than waiting for an 80-byte window.
bool responderOn=false;
unsigned long armedUntil=0;
uint8_t txSequence=10;
void responderListen(){
  radio.standby();uint8_t sync[]={0x00,0x2d,0x44,0xd2,0xef};
  radio.setSyncWord(sync,5);radio.fixedPacketLengthMode(52);
  radio.setPacketReceivedAction(onRx);rxFlag=false;radio.startReceive();
}
void responderStart(){
  armedUntil=0;win={};win.on=true; // suppress background sweeps
  radio.standby();int st=radio.beginFSK(868.40,40.0,20.0,117.3,0,80,TCXO_V);
  radio.setDio2AsRfSwitch(true);radio.setCRC(0);radio.setWhitening(false);radio.setDataShaping(RADIOLIB_SHAPING_NONE);
  responderOn=st==0;if(responderOn)responderListen();
  Serial.printf("{\"t\":\"danfoss_observe\",\"status\":%d,\"armed\":false}\n",st);
}
int sendFrame(const uint8_t*f,size_t n){
  radio.standby();radio.clearPacketReceivedAction();uint8_t sync[]={0xaa,0x0f};
  int st=radio.setSyncWord(sync,2);if(st==0)st=radio.fixedPacketLengthMode(n);
  uint8_t wire[64];for(size_t i=0;i<n;i++)wire[i]=f[i]^255;
  return st==0?radio.transmit(wire,n):st;
}
void responderPacket(){
  rxFlag=false;uint32_t received=micros();uint8_t raw[52],frame[57]={0xff,0xd2,0xbb,0x2d,16};
  int st=radio.readData(raw,52);float rssi=radio.getRSSI();
  for(int i=0;i<52;i++)frame[i+5]=raw[i]^255;
  size_t actual=frame[7];
  bool valid=st==0 && actual<=57 && Danfoss::validReport(frame,actual,danfossKey);
  bool fire=valid && armedUntil && (int32_t)(armedUntil-millis())>0;
  uint8_t ack[10],response[37];int as=-999,rs=-999;uint32_t latency=0;
  if(fire){
    armedUntil=0; // consume arm before any transmission, even on radio error
    Danfoss::reply(frame,danfossKey,txSequence++&15,ack,response);
    latency=micros()-received;as=sendFrame(ack,10);
    if(as==0){delayMicroseconds(1500);rs=sendFrame(response,37);}
  }
  responderListen();
  Serial.printf("{\"t\":\"danfoss_report\",\"valid\":%s,\"rssi\":%.1f,\"hex\":\"",valid?"true":"false",rssi);hex(frame,57);Serial.println("\"}");
  if(fire){Serial.printf("{\"t\":\"danfoss_response\",\"ack_status\":%d,\"response_status\":%d,\"processing_us\":%lu,\"hex\":\"",as,rs,(unsigned long)latency);hex(response,37);Serial.println("\"}");}
}

// Commands from the Pi, one per line:
//   LORA <MHz> <bw kHz> <sf> <cr> <sync hex> <preamble> <crc 0/1> <iq 0/1> <ldro -1/0/1> <seconds> <tag>
//   FSK <MHz> <bitrate kbps> <deviation kHz> <rx bw kHz> <sync hex> <packet len> <seconds> <tag>
//   SWEEP <start MHz> <stop MHz> <step MHz>   one-off sweep of any range (150-960 MHz), logged as "xsweep"
//   STOP
void command(char *line) {
  if (!strcmp(line,"DANFOSS_OBSERVE")) { responderStart(); return; }
  if (!strcmp(line,"DANFOSS_ARM")) {
    if (responderOn) { armedUntil=millis()+600000; Serial.println("{\"t\":\"danfoss_arm\",\"seconds\":600,\"bursts\":1}"); }
    return;
  }
  if (!strcmp(line,"DANFOSS_DISARM")) {armedUntil=0;Serial.println("{\"t\":\"danfoss_disarmed\"}");return;}
  responderOn=false;armedUntil=0;
  LoraCfg c;
  int crc, iq, secs;
  char tag[24];
  float a, b, st;
  if (sscanf(line, "LORA %f %f %d %d %x %d %d %d %d %d %23s", &c.freq, &c.bw, &c.sf, &c.cr, &c.sync, &c.pre, &crc,
             &iq, &c.ldro, &secs, tag) == 11) {
    c.crc = crc;
    c.iq = iq;
    win = {};
    win.on = true;
    win.cfg = c;
    strcpy(win.tag, tag);
    win.end = millis() + secs * 1000UL;
    applyLora(c);
    Serial.printf("{\"t\":\"window\",\"ms\":%lu,\"tag\":\"%s\",\"freq\":%.4f,\"bw\":%.1f,\"sf\":%d,\"cr\":%d,\"secs\":%d}\n",
                  millis(), tag, c.freq, c.bw, c.sf, c.cr, secs);
  } else if (strncmp(line, "FSK ", 4) == 0) {
    FskCfg f;
    char syncHex[17];
    if (sscanf(line, "FSK %f %f %f %f %16s %d %d %23s", &f.freq, &f.br, &f.dev, &f.rxbw, syncHex, &f.len, &secs,
               tag) != 8) {
      Serial.printf("{\"t\":\"err\",\"where\":\"cmd\",\"line\":\"%.40s\"}\n", line);
      return;
    }
    f.syncLen = strlen(syncHex) / 2;
    for (int i = 0; i < f.syncLen; i++) sscanf(syncHex + 2 * i, "%2hhx", &f.sync[i]);
    win = {};
    win.on = win.fsk = true;
    win.fcfg = f;
    strcpy(win.tag, tag);
    win.end = millis() + secs * 1000UL;
    applyFsk(f);
    Serial.printf("{\"t\":\"window\",\"ms\":%lu,\"tag\":\"%s\",\"mode\":\"fsk\",\"freq\":%.4f,\"br\":%.1f,\"len\":%d,\"secs\":%d}\n",
                  millis(), tag, f.freq, f.br, f.len, secs);
  } else if (sscanf(line, "SWEEP %f %f %f", &a, &b, &st) == 3 && a >= 150 && b <= 960 && a < b && st >= 0.01 &&
             (b - a) / st <= 2000) {
    sweepRange("xsweep", a, b, st);
    if (win.on && win.fsk) applyFsk(win.fcfg);
    else applyLora(win.on ? win.cfg : meshCfg);
  } else if (strncmp(line, "STOP", 4) == 0) {
    win.end = millis();
  } else {
    Serial.printf("{\"t\":\"err\",\"where\":\"cmd\",\"line\":\"%.40s\"}\n", line);
  }
}

void readCommands() {
  static char buf[128];
  static size_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      buf[n] = 0;
      if (n) command(buf);
      n = 0;
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = ch;
    }
  }
}

// Count weak-signal evidence during a window and close it when time is up.
void windowTick() {
  if (!win.on) return;
  uint32_t irq = win.fsk ? 0 : radio.getIrqFlags();
  if (irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED) {
    win.preambles++;
    radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
  }
  if (irq & RADIOLIB_SX126X_IRQ_HEADER_VALID) {
    win.headers++;
    radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_HEADER_VALID);
  }
  if ((long)(millis() - win.end) >= 0) {
    Serial.printf("{\"t\":\"window_end\",\"ms\":%lu,\"tag\":\"%s\",\"packets\":%lu,\"preambles\":%lu,\"headers\":%lu}\n",
                  millis(), win.tag, win.packets, win.preambles, win.headers);
    bool wasFsk = win.fsk;
    win.on = win.fsk = false;
    if (wasFsk) startMesh();
    else applyLora(meshCfg);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(P_VEXT, OUTPUT);
  digitalWrite(P_VEXT, LOW);  // power the OLED
  pinMode(P_OLED_RST, OUTPUT);
  digitalWrite(P_OLED_RST, LOW);
  delay(20);
  digitalWrite(P_OLED_RST, HIGH);
  oled.init();
  oled.flipScreenVertically();

  spi.begin(P_SCK, P_MISO, P_MOSI, P_NSS);
  bool ok = startMesh();
  Serial.printf("{\"t\":\"boot\",\"fw\":\"0.5\",\"radio\":%s,\"freq\":%.3f,\"bw\":%.1f,\"sf\":%d,\"cr\":%d}\n",
                ok ? "true" : "false", MESH_FREQ, MESH_BW, MESH_SF, MESH_CR);
  ridBegin();
  drawStatus();
}

void loop() {
  readCommands();
  if(responderOn){
    if(armedUntil && (int32_t)(armedUntil-millis())<=0){armedUntil=0;Serial.println("{\"t\":\"danfoss_arm_expired\"}");}
    if(rxFlag)responderPacket();
    return;
  }
  windowTick();
  ridPoll();
  if (rxFlag) {
    handlePacket();
    drawStatus();
  }
  // Don't sweep in the middle of a packet being received.
  if (millis() - lastSweep >= SWEEP_PERIOD_MS && !rxFlag && !win.on) {
    lastSweep = millis();
    sweep();
    uint32_t wf, ba, rm;
    ridStats(wf, ba, rm);
    Serial.printf("{\"t\":\"hb\",\"ms\":%lu,\"pkts\":%lu,\"crc\":%lu,\"wifi_frames\":%lu,\"ble_adv\":%lu,\"rid_msgs\":%lu,\"heap\":%u}\n",
                  millis(), meshPackets, crcErrors, wf, ba, rm, ESP.getFreeHeap());
    drawStatus();
  }
}
