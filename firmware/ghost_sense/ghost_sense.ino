#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_wifi.h"
#include "secrets.h"
const char* SSID = WIFI_SSID;
const char* PASS = WIFI_PASS;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
WiFiUDP udp;

#define MAXSC 192
float prevAmp[MAXSC];
uint32_t prevT = 0;
int prevLen = 0;
uint8_t bssid[6];

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile float turbSum = 0, turbMax = 0;
volatile uint32_t turbCnt = 0;
volatile float rssiSum = 0, rssiSq = 0;
volatile uint32_t rssiCnt = 0;
volatile uint32_t pktCount = 0;
int prevKey = -1;
volatile uint32_t keyChanges = 0;
volatile int lastSig = 0, lastMcs = 0, lastCwb = 0, lastNoise = 0;

void csiCallback(void *ctx, wifi_csi_info_t *info) {
  if (!info || !info->buf) return;
  if (memcmp(info->mac, bssid, 6) != 0) return;

  int n = info->len / 2;
  if (n > MAXSC) n = MAXSC;
  if (n < 10) return;

  int8_t *d = info->buf;
  float amp[MAXSC];
  float mean = 0;
  for (int i = 0; i < n; i++) {
    float im = d[2 * i], re = d[2 * i + 1];
    amp[i] = sqrtf(re * re + im * im);
    mean += amp[i];
  }
  mean /= n;
  if (mean < 1) return;
  int key = (info->rx_ctrl.sig_mode << 12) | (info->rx_ctrl.mcs << 4) | (info->rx_ctrl.cwb << 1) | info->rx_ctrl.stbc;
  if (key != prevKey) keyChanges++;
  lastSig = info->rx_ctrl.sig_mode; lastMcs = info->rx_ctrl.mcs;
  lastCwb = info->rx_ctrl.cwb; lastNoise = info->rx_ctrl.noise_floor;
  uint32_t now = millis();
  pktCount++;
  float rssi = info->rx_ctrl.rssi;

  portENTER_CRITICAL(&mux);
  rssiSum += rssi; rssiSq += rssi * rssi; rssiCnt++;
  portEXIT_CRITICAL(&mux);
  prevKey = key;
    if (prevLen == info->len && prevKey == key && (now - prevT) < 200) {
    float diff = 0;
    for (int i = 0; i < n; i++) diff += fabsf(amp[i] - prevAmp[i]);
    float t = (diff / n) / mean;
    portENTER_CRITICAL(&mux);
    turbSum += t;
    if (t > turbMax) turbMax = t;
    turbCnt++;
    portEXIT_CRITICAL(&mux);
  }
  for (int i = 0; i < n; i++) prevAmp[i] = amp[i];
  prevLen = info->len;
  prevT = now;
}

void startCSI() {
  wifi_csi_config_t csi = {};
  csi.lltf_en = true;
  csi.htltf_en = true;
  csi.stbc_htltf2_en = true;
  csi.ltf_merge_en = true;
  csi.channel_filter_en = true;
  csi.manu_scale = false;
  csi.shift = 0;
  esp_wifi_set_csi_config(&csi);
  esp_wifi_set_csi_rx_cb(csiCallback, NULL);
  esp_wifi_set_csi(true);
}

void show(const char* big, const char* small, int secsLeft, uint32_t rate) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GHOST SENSE");
  display.print(small); display.print(" "); display.print(secsLeft);
  display.print("s  pk/s:"); display.println(rate);
  display.setTextSize(3);
  display.setCursor(0, 32);
  display.println(big);
  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay(); display.setCursor(0, 0);
  display.println("Connecting WiFi..."); display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  WiFi.setSleep(false);
  memcpy(bssid, WiFi.BSSID(), 6);
  udp.begin(4210);
  startCSI();

  Serial.print("IP: "); Serial.println(WiFi.localIP());
  Serial.println("t,label,pkts,used,score_avg,score_max,rssi_mean,rssi_sd");
}

void loop() {
  static uint32_t startMs = 0;
  static uint32_t lastPkts = 0;
  static bool started = false;

  // Wait 1 second while draining the UDP packets coming from the Mac
  uint32_t t0 = millis();
  while (millis() - t0 < 1000) {
    if (udp.parsePacket()) { uint8_t b[64]; udp.read(b, sizeof(b)); }
    else delay(1);
  }

  float s, smax, rs, rq; uint32_t c, rc;
  portENTER_CRITICAL(&mux);
  s = turbSum; smax = turbMax; c = turbCnt;
  rs = rssiSum; rq = rssiSq; rc = rssiCnt;
  turbSum = 0; turbMax = 0; turbCnt = 0;
  rssiSum = 0; rssiSq = 0; rssiCnt = 0;
  portEXIT_CRITICAL(&mux);

  uint32_t p = pktCount;
  uint32_t rate = p - lastPkts;
  lastPkts = p;

  // Start the timeline only once the Mac stream is flowing (>60 pkts/s)
  if (!started) {
    if (rate > 60) { started = true; startMs = millis(); }
    else { show("WAIT", "start sender", 0, rate); Serial.printf("waiting for sender... pkts/s=%lu\n", rate); return; }
  }


      float rm = rc > 0 ? rs / rc : 0;
  float rsd = rc > 1 ? sqrtf(fmaxf(0.0f, rq / rc - rm * rm)) : 0;

  static float base = 0;
  static bool haveBase = false;
  static int hist[5] = {0, 0, 0, 0, 0};
  static int hi = 0;

  bool valid = rc >= 20;
  if (valid && !haveBase) { base = rm; haveBase = true; }
  float dev = valid ? fabsf(rm - base) : 0;

  hist[hi] = (valid && (dev > 1.0f || rsd > 1.8f)) ? 1 : 0;
  hi = (hi + 1) % 5;
  int sum = 0;
  for (int i = 0; i < 5; i++) sum += hist[i];
  bool motion = sum >= 2;

  if (valid && !motion) base += 0.02f * (rm - base);   // slowly follow drift

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("GHOST SENSE pk:"); display.println(rate);
  display.print("dev "); display.print(dev, 1);
  display.print(" sd "); display.println(rsd, 1);
  display.setTextSize(3);
  display.setCursor(0, 30);
  display.println(motion ? "MOTION" : "CLEAR");
  display.display();

  Serial.printf("rssi=%.1f base=%.1f dev=%.2f sd=%.2f motion=%d\n", rm, base, dev, rsd, motion);
}