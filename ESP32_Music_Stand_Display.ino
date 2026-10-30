/*
 * AbleSet stage display — Waveshare ESP32-C6 LCD 1.47"
 * -----------------------------------------------------
 * Copyright (c) 2026 by Dacey Bell (daceybell@duck.com)
 * https://firstwest.cc & https://daceybell.weebly.com/
 * -----------------------------------------------------
 * Connects to a WiFi network (2.4 GHz), subscribes to AbleSet's OSC
 * feed, and shows current song and current measure.
 *
 * Board:    ESP32-C6 Dev Module (ST7789, 172x320)
 * Tools menu: Board = "ESP32C6 Dev Module", USB CDC On Boot = Enabled
 */

#include <WiFi.h>
#include <ESPmDNS.h>
#include <lwip/sockets.h>
#include <math.h>
#include <Arduino_GFX_Library.h>

// Explicitly link Adafruit GFX to resolve font header search paths
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold10pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// Include external network settings
#include "user_wifi_setup.h"

// ============================================================================
// HARDWARE DISPLAY SETUP (ESP32-C6)
// ============================================================================

#define LCD_DC    15
#define LCD_CS    14
#define LCD_CLK    1
#define LCD_MOSI   2
#define LCD_RST   22
#define LCD_BL    23
#define RGB_LED    8
#define RETRY_BTN_PIN 9   // GPIO 9 (Default ESP32-C6 BOOT button)

// Standard 5-parameter Arduino_ESP32SPI constructor
Arduino_DataBus *bus = new Arduino_ESP32SPI(
    LCD_DC, LCD_CS, LCD_CLK, LCD_MOSI, GFX_NOT_DEFINED
);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus,
    LCD_RST,
    7,     // Rotation 7 (Landscape un-mirrored)
    true,  // IPS panel
    172,   // Width
    320,   // Height
    34,    // Col offset 1
    0,     // Row offset 1
    34,    // Col offset 2
    0      // Row offset 2
);

static const int SCR_W = 320;
static const int SCR_H = 172;

// Zones — Layout Optimized for 2-Line Song Titles & Scaled Large Measures
static const int Z_STATUS_Y = 0,   Z_STATUS_H = 16;
static const int Z_SONG_Y   = 16,  Z_SONG_H   = 52; 
static const int Z_MEAS_Y   = 68,  Z_MEAS_H   = 104; // Zone height for large measures

static const uint16_t COL_BG       = 0xFFFF; // Main background BLACK
static const uint16_t COL_TEXT     = 0x0000; // Main text WHITE
static const uint16_t COL_SONG_BG  = 0xFFFF; // BLACK box behind song title
static const uint16_t COL_DIM      = 0x8410; // Medium Grey
static const uint16_t COL_OK       = 0xE007; // GREEN   - Connected
static const uint16_t COL_WARN     = 0xE0FF; // ORANGE  - AbleSet Linked / Ableton Offline
static const uint16_t COL_ERR      = 0xFC00; // RED     - OFF-LINE

// ============================================================================
// STATE & NETWORKING
// ============================================================================

int oscSock = -1;
SemaphoreHandle_t stateLock = nullptr;
IPAddress abletonIp;
volatile bool haveAblesetIp = false;
String connectedSsid = "";

struct State {
  String song      = "";
  int    songIndex  = -1;
  bool   liveLinked = false;
  int    measure   = 0;
} net, st, drawn;

bool wifiUp = false;
bool absetUp = false;
bool mdnsUp = false;
bool drawnWifiUp = false, drawnAbsetUp = false;
bool forceRedraw = true;

volatile uint32_t lastPacketMs = 0;
uint32_t lastSubscribe  = 0;
uint32_t lastWifiCheck  = 0;
uint32_t lastWifiRetry  = 0;
uint32_t lastKeepalive  = 0;
uint32_t lastResync     = 0;

static const uint32_t ABLESET_TIMEOUT_MS = 10000;
static const uint32_t RESUBSCRIBE_MS     = 5000;
static const uint32_t KEEPALIVE_MS       = 2000;
static const uint32_t RESYNC_MS          = 10000;

static const uint32_t ABLESET_HUNT_MS    = 30000;
uint32_t wifiConnectedAt = 0;
int wifiRotate = 0;

static const int MAX_NETWORK_TRIES = WIFI_ROUNDS * N_WIFI;
int networkTries = 0;
bool anyWifiThisRun = false;
bool gaveUp = false;

// Forward Declarations
static void startNetworking();

// ============================================================================
// OSC TYPES
// ============================================================================

struct OscOut {
  uint8_t d[512];
  size_t n = 0;
  void reset() { n = 0; }
  void str(const char *s) {
    size_t l = strlen(s);
    if (n + l + 4 > sizeof(d)) return;
    memcpy(d + n, s, l); n += l; d[n++] = 0;
    while (n % 4) d[n++] = 0;
  }
  void i32(int32_t v) {
    if (n + 4 > sizeof(d)) return;
    d[n++] = (v >> 24) & 0xFF; d[n++] = (v >> 16) & 0xFF;
    d[n++] = (v >> 8) & 0xFF;  d[n++] = v & 0xFF;
  }
};

struct OscArg {
  char type = 0;
  const char *s = nullptr;
  int32_t i = 0;
  float f = 0;
};

// ============================================================================
// TEXT RENDERING (SANS-SERIF WRAPPING & SCALING)
// ============================================================================

static void drawTextBlock(int x, int y, int w, int h, const String &txt,
                          uint16_t fg, uint16_t bg, const GFXfont *font = nullptr, int fontScale = 1, int maxLines = 2) {
  gfx->fillRect(x, y, w, h, bg);
  if (txt.length() == 0) return;

  gfx->setFont(font);
  gfx->setTextSize(fontScale);
  gfx->setTextColor(fg);
  gfx->setTextWrap(false);

  // Default bitmap font handling
  if (!font) {
    int16_t x1, y1; uint16_t tw, th;
    gfx->getTextBounds(txt, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor(x + (w - (int)tw) / 2 - x1, y + (h - (int)th) / 2 - y1);
    gfx->print(txt);
    return;
  }

  // Word-wrapping logic into 1 or 2 lines
  String lines[2];
  int lineCount = 0;

  int16_t x1, y1; uint16_t tw, th;
  gfx->getTextBounds(txt, 0, 0, &x1, &y1, &tw, &th);

  // If text fits on a single line
  if (tw <= w || maxLines == 1) {
    lines[0] = txt;
    lineCount = 1;
  } else {
    // Split into 2 lines near middle space
    int mid = txt.length() / 2;
    int bestSpace = -1;
    int minDist = 999;

    for (int i = 0; i < txt.length(); i++) {
      if (txt[i] == ' ') {
        int dist = abs(i - mid);
        if (dist < minDist) {
          minDist = dist;
          bestSpace = i;
        }
      }
    }

    if (bestSpace != -1) {
      lines[0] = txt.substring(0, bestSpace);
      lines[1] = txt.substring(bestSpace + 1);
      lineCount = 2;
    } else {
      lines[0] = txt;
      lineCount = 1;
    }
  }

  // Measure font line height
  gfx->getTextBounds("A", 0, 0, &x1, &y1, &tw, &th);
  int singleLineH = th;
  int lineSpacing = singleLineH + 4;
  int totalHeight = (lineCount == 1) ? singleLineH : (singleLineH + lineSpacing);

  // Draw rendered lines
  int startY = y + (h - totalHeight) / 2 + singleLineH;

  for (int i = 0; i < lineCount; i++) {
    uint16_t lw, lh;
    gfx->getTextBounds(lines[i], 0, 0, &x1, &y1, &lw, &lh);
    int tx = x + (w - (int)lw) / 2 - x1;
    int ty = startY + (i * lineSpacing);

    gfx->setCursor(tx, ty);
    gfx->print(lines[i]);
  }
}

static String cleanName(const String &in) {
  String name;
  int i = 0, len = in.length();
  while (i < len) {
    if (in[i] == '{') {
      int j = in.indexOf('}', i + 1);
      i = (j < 0) ? len : j + 1;
    } else {
      name += in[i++];
    }
  }
  name.trim();
  while (name.indexOf("  ") >= 0) name.replace("  ", " ");
  return name;
}

static uint32_t nextCodepoint(const String &s, unsigned &i) {
  uint8_t c = (uint8_t)s[i++];
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  uint32_t cp = c & (0x3F >> extra);
  while (extra-- > 0 && i < s.length()) cp = (cp << 6) | ((uint8_t)s[i++] & 0x3F);
  return cp;
}

static String displayText(const String &in) {
  static const char FOLD[] =
    "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTS"
    "AAAAAAACEEEEIIII" "DNOOOOO/OUUUUYTY";
  String s = cleanName(in);
  String out;
  unsigned i = 0;
  while (i < s.length()) {
    uint32_t cp = nextCodepoint(s, i);
    if (cp >= 'a' && cp <= 'z')                 out += (char)(cp - 32);
    else if (cp >= 0x20 && cp < 0x7F)           out += (char)cp;
    else if (cp == 0xC6 || cp == 0xE6)          out += "AE";
    else if (cp == 0xDF)                        out += "SS";
    else if (cp == 0x152 || cp == 0x153)        out += "OE";
    else if (cp >= 0xC0 && cp <= 0xFF)          out += FOLD[cp - 0xC0];
    else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) out += '-';
    else if (cp >= 0x2018 && cp <= 0x201B)      out += '\'';
    else if (cp >= 0x201C && cp <= 0x201F)      out += '"';
    else if (cp == 0x2026)                      out += "...";
    else if (cp == 0xA0)                        out += ' ';
  }
  out.trim();
  return out;
}

static void drawStatusBar() {
  gfx->fillRect(0, Z_STATUS_Y, SCR_W, Z_STATUS_H, COL_BG);
  gfx->setTextWrap(false);
  gfx->setFont(NULL);
  gfx->setTextSize(1);

  uint16_t wifiCol  = wifiUp ? COL_OK : COL_ERR;
  uint16_t absetCol = absetUp ? (st.liveLinked ? COL_OK : COL_WARN) : COL_ERR;
  gfx->fillCircle(10, Z_STATUS_Y + 8, 4, wifiCol);
  gfx->fillCircle(26, Z_STATUS_Y + 8, 4, absetCol);
}

static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!USE_RGB_LED) return;
  rgbLedWrite(RGB_LED, r, g, b);
}

static void render() {
  if (forceRedraw || wifiUp != drawnWifiUp || absetUp != drawnAbsetUp ||
      st.songIndex != drawn.songIndex || st.liveLinked != drawn.liveLinked) {
    drawStatusBar();
    drawnWifiUp = wifiUp; drawnAbsetUp = absetUp;
    drawn.songIndex = st.songIndex; drawn.liveLinked = st.liveLinked;
  }

  // Active Song Title (FreeSansBold10pt7b — 2 Lines, Scale 1)
  if (forceRedraw || st.song != drawn.song) {
    String s = displayText(st.song);
    uint16_t c = COL_TEXT;
    if (!s.length()) {
      s = !wifiUp ? "NO WIFI" : (absetUp ? "--" : "ABLESET OFFLINE");
      c = COL_ERR;
    }
    gfx->fillRoundRect(4, Z_SONG_Y, SCR_W - 8, Z_SONG_H, 6, COL_SONG_BG);
    drawTextBlock(6, Z_SONG_Y + 2, SCR_W - 12, Z_SONG_H - 4, s, c, COL_SONG_BG, &FreeSansBold10pt7b, 1, 2);
    drawn.song = st.song;
  }

  // Thin dividing line between song title area and measure number area
  if (forceRedraw) {
    gfx->drawFastHLine(0, 68, SCR_W, COL_DIM);
  }

  // Large Current Measure Display (FreeSansBold24pt7b — Scaled 2x to ~48pt)
  if (forceRedraw || st.measure != drawn.measure) {
    String mStr = (st.measure > 0) ? (String(st.measure)) : "--";
    drawTextBlock(0, Z_MEAS_Y + 1, SCR_W, Z_MEAS_H - 1, mStr, COL_TEXT, COL_BG, &FreeSansBold24pt7b, 2, 1);
    drawn.measure = st.measure;
  }

  forceRedraw = false;

  if (!wifiUp)            setLed(20, 0, 0);
  else if (!absetUp)      setLed(20, 8, 0);
  else                    setLed(0, 0, 10);
}

// ============================================================================
// OSC ENCODER / DECODER
// ============================================================================

static IPAddress broadcastIp() {
  IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  IPAddress b;
  for (int i = 0; i < 4; i++) b[i] = ip[i] | (~mask[i] & 0xFF);
  return b;
}

static void oscSend(OscOut &m) {
  if (!wifiUp || oscSock < 0) return;
  IPAddress dst = broadcastIp();
  if (haveAblesetIp && (uint32_t)abletonIp != 0) dst = abletonIp;
  struct sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(ABLESET_PORT);
  to.sin_addr.s_addr = (uint32_t)dst;
  sendto(oscSock, m.d, m.n, 0, (struct sockaddr *)&to, sizeof(to));
}

static const char *FILTERS[] = {
  "/setlist/activeSongName",
  "/setlist/activeSongIndex",
  "/global/currentMeasure",
  "/global/isConnected",
  "/heartbeat",
};
static const int N_FILTERS = sizeof(FILTERS) / sizeof(FILTERS[0]);

static String hostnameFromDeviceName() {
  String h;
  for (size_t i = 0; DEVICE_NAME[i]; i++) {
    char c = DEVICE_NAME[i];
    if (c == ' ' || c == '_' || c == '.') c = '-';
    else if (c >= 'A' && c <= 'Z') c += 32;
    else if (!isalnum((int)c) && c != '-') continue;
    h += c;
  }
  if (h.length() == 0) h = "ableset-display";
  return h;
}

static void sendGetValues() {
  OscOut g;
  g.str("/getValues");
  g.str(",i");
  g.i32(LOCAL_PORT);
  oscSend(g);
}

static const char *RESYNC_VALUES[] = {
  "/setlist/activeSongName",
  "/setlist/activeSongIndex",
  "/global/currentMeasure",
  "/global/isConnected",
};
static const int N_RESYNC = sizeof(RESYNC_VALUES) / sizeof(RESYNC_VALUES[0]);

static void sendResync() {
  OscOut g;
  g.str("/getValues");
  String tt = ",";
  for (int i = 0; i < N_RESYNC; i++) tt += "s";
  g.str(tt.c_str());
  for (int i = 0; i < N_RESYNC; i++) g.str(RESYNC_VALUES[i]);
  oscSend(g);
}

static void sendSubscribe() {
  OscOut m;
  m.str("/subscribe");
  String tt = ",isT"; // Enabled high-rate real-time updates ('T')
  if (USE_FILTERS) for (int i = 0; i < N_FILTERS; i++) tt += "s";
  m.str(tt.c_str());
  m.i32(LOCAL_PORT);
  m.str(DEVICE_NAME);
  if (USE_FILTERS) for (int i = 0; i < N_FILTERS; i++) m.str(FILTERS[i]);
  oscSend(m);

  sendGetValues();
}

static float argNum(const OscArg &a) {
  if (a.type == 'i') return (float)a.i;
  if (a.type == 'f') return a.f;
  if (a.type == 'T') return 1;
  if (a.type == 'F') return 0;
  if (a.type == 's' && a.s) return atof(a.s);
  return 0;
}

static void handleMessage(const char *addr, OscArg *args, int nArgs) {
  if (!strcmp(addr, "/heartbeat")) return;

  if (!strcmp(addr, "/setlist/activeSongName")) {
    net.song = (nArgs > 0 && args[0].type == 's') ? String(args[0].s) : String("");
  } else if (!strcmp(addr, "/setlist/activeSongIndex")) {
    net.songIndex = nArgs > 0 ? (int)argNum(args[0]) : -1;
  } else if (!strcmp(addr, "/global/currentMeasure")) {
    net.measure = nArgs > 0 ? (int)argNum(args[0]) : 0;
  } else if (!strcmp(addr, "/global/isConnected")) {
    net.liveLinked = nArgs > 0 && argNum(args[0]) > 0.5f;
  }
}

static void parseElement(const uint8_t *p, size_t len) {
  if (len < 4) return;

  const char *addr = (const char *)p;
  size_t alen = strnlen(addr, len);
  if (alen >= len) return;
  size_t off = (alen + 4) & ~3u;
  if (off >= len) { handleMessage(addr, (OscArg *)nullptr, 0); return; }

  const char *tt = (const char *)(p + off);
  if (tt[0] != ',') return;
  size_t tlen = strnlen(tt, len - off);
  if (off + tlen >= len) return;
  off += (tlen + 4) & ~3u;

  OscArg args[8];
  int nArgs = 0;
  for (size_t i = 1; i < tlen && nArgs < 8; i++) {
    char t = tt[i];
    OscArg a; a.type = t;
    if (t == 's' || t == 'S') {
      if (off >= len) break;
      a.s = (const char *)(p + off);
      size_t sl = strnlen(a.s, len - off);
      off += (sl + 4) & ~3u;
    } else if (t == 'i' || t == 'f' || t == 'c' || t == 'r' || t == 'm') {
      if (off + 4 > len) break;
      uint32_t v = ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16) |
                   ((uint32_t)p[off + 2] << 8) | (uint32_t)p[off + 3];
      if (t == 'f') memcpy(&a.f, &v, 4); else a.i = (int32_t)v;
      off += 4;
    } else if (t == 'h' || t == 'd' || t == 't') {
      if (off + 8 > len) break;
      off += 8;
    } else if (t == 'b') {
      if (off + 4 > len) break;
      uint32_t bl = ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16) |
                    ((uint32_t)p[off + 2] << 8) | (uint32_t)p[off + 3];
      off += 4 + ((bl + 3) & ~3u);
    }
    args[nArgs++] = a;
  }
  handleMessage(addr, args, nArgs);
}

static void parsePacket(const uint8_t *p, size_t len) {
  if (len >= 8 && !memcmp(p, "#bundle", 7)) {
    size_t off = 16;
    while (off + 4 <= len) {
      uint32_t sz = ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16) |
                    ((uint32_t)p[off + 2] << 8) | (uint32_t)p[off + 3];
      off += 4;
      if (sz == 0 || off + sz > len) break;
      parsePacket(p + off, sz);
      off += sz;
    }
  } else {
    parseElement(p, len);
  }
}

// ============================================================================
// RETRY & RECOVERY UI
// ============================================================================

static bool wasPressed = true;
static bool retryPressed() {
  bool down = digitalRead(RETRY_BTN_PIN) == LOW;
  bool fired = down && !wasPressed;
  wasPressed = down;
  return fired;
}

static void drawRetryScreen() {
  gfx->fillScreen(COL_BG);

  String title = anyWifiThisRun ? "ABLESET NOT FOUND" : "NO WIFI";
  String names;
  for (int i = 0; i < N_WIFI; i++) {
    if (i) names += (i == N_WIFI - 1) ? " & " : ", ";
    names += WIFI_NETWORKS[i].ssid;
  }
  String detail = String("Tried ") + names + (WIFI_ROUNDS == 2 ? " twice" : "");
  detail.toUpperCase();

  drawTextBlock(4, 8, SCR_W - 8, 36, title, COL_ERR, COL_BG, &FreeSansBold10pt7b, 1, 1);
  drawTextBlock(4, 46, SCR_W - 8, 30, displayText(detail), COL_DIM, COL_BG, NULL, 1, 1);

  const int bw = 170, bh = 50, bx = (SCR_W - bw) / 2, by = 88;
  gfx->fillRoundRect(bx, by, bw, bh, 10, COL_TEXT);
  drawTextBlock(bx + 8, by + 6, bw - 16, bh - 12, "RETRY", COL_BG, COL_TEXT, &FreeSansBold10pt7b, 1, 1);

  drawTextBlock(4, 148, SCR_W - 8, 18, "PRESS BOOT TO TRY AGAIN", COL_DIM, COL_BG, NULL, 1, 1);
}

static void giveUp() {
  gaveUp = true;
  wifiUp = false;
  haveAblesetIp = false;
  connectedSsid = "";
  if (mdnsUp) { MDNS.end(); mdnsUp = false; }
  WiFi.disconnect(true);
  wasPressed = true;
  setLed(20, 0, 0);
  drawRetryScreen();
}

static void splash(const String &line1, const String &line2, uint16_t col) {
  gfx->fillScreen(COL_BG);
  drawTextBlock(4, 40, SCR_W - 8, 50, displayText(line1), col, COL_BG, &FreeSansBold10pt7b, 1, 1);
  drawTextBlock(4, 95, SCR_W - 8, 40, displayText(line2), COL_TEXT, COL_BG, NULL, 1, 1);
}

static uint32_t packetAgeMs() {
  return millis() - lastPacketMs;
}

static void setAblesetIp(const IPAddress &ip) {
  xSemaphoreTake(stateLock, portMAX_DELAY);
  abletonIp = ip;
  haveAblesetIp = true;
  xSemaphoreGive(stateLock);
}

static bool discoverAbleset() {
  // 1. Hardcoded host override check
  if (strlen(ABLESET_HOST) > 0) {
    IPAddress fixed;
    if (fixed.fromString(ABLESET_HOST)) {
      setAblesetIp(fixed);
      return true;
    }
  }

  // 2. Select target IP based on connected SSID
  for (int i = 0; i < N_WIFI; i++) {
    if (connectedSsid == WIFI_NETWORKS[i].ssid && strlen(WIFI_NETWORKS[i].ablesetIp) > 0) {
      IPAddress targetIp;
      if (targetIp.fromString(WIFI_NETWORKS[i].ablesetIp)) {
        setAblesetIp(targetIp);
        return true;
      }
    }
  }

  // 3. Fallback: Auto-discover via mDNS
  int n = MDNS.queryService("osc", "udp");
  for (int i = 0; i < n; i++) {
    IPAddress a = MDNS.address(i);
    String hn = MDNS.hostname(i);
    if (MDNS.port(i) != ABLESET_PORT) continue;

    if ((uint32_t)a == 0 && hn.length() > 0) {
      IPAddress r = MDNS.queryHost(hn.c_str(), 3000);
      a = r;
    }

    if ((uint32_t)a != 0) {
      setAblesetIp(a);
      return true;
    }
  }
  return false;
}

static bool tryNetwork(int idx) {
  const WifiNetwork &net = WIFI_NETWORKS[idx];
  splash("CONNECTING", net.ssid, COL_WARN);

  WiFi.begin(net.ssid, net.pass);
  uint32_t t0 = millis();
  while (millis() - t0 < WIFI_TRY_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      connectedSsid = net.ssid;
      return true;
    }
    delay(100);
  }
  WiFi.disconnect(false, true);
  delay(100);
  return false;
}

static void scanVisible(bool *visible) {
  splash("SCANNING", "for known networks", COL_WARN);
  int found = WiFi.scanNetworks();

  int nVisible = 0;
  for (int i = 0; i < N_WIFI; i++) {
    int hit = -1;
    for (int j = 0; j < found; j++) {
      if (WiFi.SSID(j) == WIFI_NETWORKS[i].ssid) { hit = j; break; }
    }
    visible[i] = hit >= 0;
    if (hit >= 0) {
      nVisible++;
    }
  }
  WiFi.scanDelete();
  if (nVisible == 0) {
    for (int i = 0; i < N_WIFI; i++) visible[i] = true;
  }
}

static bool connectWifi() {
  connectedSsid = "";
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  String host = hostnameFromDeviceName();
  WiFi.setHostname(host.c_str());

  bool visible[N_WIFI];
  bool scanned = false;

  while (networkTries < MAX_NETWORK_TRIES) {
    if (!scanned || wifiRotate % N_WIFI == 0) {
      scanVisible(visible);
      scanned = true;
    }
    int idx = wifiRotate % N_WIFI;
    wifiRotate++;
    networkTries++;
    if (!visible[idx]) continue;

    if (tryNetwork(idx)) {
      anyWifiThisRun = true;
      wifiUp = true;
      wifiConnectedAt = millis();
      if (mdnsUp) { MDNS.end(); mdnsUp = false; }
      MDNS.begin(host.c_str());
      mdnsUp = true;
      startNetworking();
      splash("LOOKING FOR", "ABLESET", COL_WARN);
      forceRedraw = true;
      return true;
    }
  }

  wifiUp = false;
  giveUp();
  return false;
}

static void retryNow() {
  gaveUp = false;
  networkTries = 0;
  anyWifiThisRun = false;
  wifiRotate = 0;
  if (connectWifi()) {
    discoverAbleset();
    sendSubscribe();
    lastSubscribe = millis();
  }
  lastWifiRetry = millis();
  lastWifiCheck = millis();
  if (!gaveUp) {
    gfx->fillScreen(COL_BG);
    forceRedraw = true;
  }
}

// ============================================================================
// NETWORK RECEIVE TASK
// ============================================================================

static void oscRxTask(void *) {
  static uint8_t buf[4096];
  for (;;) {
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int len = recvfrom(oscSock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
    if (len <= 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    IPAddress src((uint32_t)from.sin_addr.s_addr);

    xSemaphoreTake(stateLock, portMAX_DELAY);
    if (!haveAblesetIp || src != abletonIp) {
      abletonIp = src;
      haveAblesetIp = true;
    }
    parsePacket(buf, len);
    lastPacketMs = millis();
    xSemaphoreGive(stateLock);
  }
}

static void startNetworking() {
  if (oscSock >= 0) return;
  oscSock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(LOCAL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (oscSock < 0 || bind(oscSock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    if (oscSock >= 0) { close(oscSock); oscSock = -1; }
    return;
  }
  int yes = 1;
  setsockopt(oscSock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
  xTaskCreate(oscRxTask, "osc_rx", 8192, nullptr, 5, nullptr);
}

static void takeSnapshot() {
  xSemaphoreTake(stateLock, portMAX_DELAY);
  State s = net;
  xSemaphoreGive(stateLock);
  st = s;
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  stateLock = xSemaphoreCreateMutex();
  pinMode(RETRY_BTN_PIN, INPUT_PULLUP);

  // Set LCD Backlight PWM to 40%
  pinMode(LCD_BL, OUTPUT);
  analogWrite(LCD_BL, (255 * BACKLIGHT_PCT) / 100);

  gfx->begin();
  gfx->fillScreen(COL_BG);
  splash("ABLESET", "starting up", COL_TEXT);

  if (connectWifi()) {
    discoverAbleset();
    sendSubscribe();
    lastSubscribe = millis();
  }
  lastWifiRetry = millis();

  if (!gaveUp) {
    gfx->fillScreen(COL_BG);
    forceRedraw = true;
  }
}

void loop() {
  if (gaveUp) {
    if (retryPressed()) retryNow();
    delay(20);
    return;
  }

  uint32_t now = millis();

  if (now - lastWifiCheck > 2000) {
    lastWifiCheck = now;
    bool up = (WiFi.status() == WL_CONNECTED);
    if (up != wifiUp) {
      wifiUp = up;
      forceRedraw = true;
      if (!up) {
        haveAblesetIp = false;
        connectedSsid = "";
        wifiRotate = (wifiRotate + N_WIFI - 1) % N_WIFI;
      }
    }
    if (!wifiUp && now - lastWifiRetry > WIFI_RETRY_MS) {
      connectWifi();
      lastWifiRetry = millis();
      if (gaveUp) return;
      gfx->fillScreen(COL_BG);
      forceRedraw = true;
      if (wifiUp) {
        discoverAbleset();
        sendSubscribe();
        lastSubscribe = millis();
      }
      now = millis();
      lastWifiCheck = now;
    }
  }

  bool up = wifiUp && haveAblesetIp && (packetAgeMs() < ABLESET_TIMEOUT_MS);
  if (up != absetUp) {
    absetUp = up;
    forceRedraw = true;
    if (up) {
      networkTries = 0;
      anyWifiThisRun = true;
    }
    if (!up) {
      xSemaphoreTake(stateLock, portMAX_DELAY);
      net.song = ""; net.measure = 0;
      xSemaphoreGive(stateLock);
    }
  }

  if (haveAblesetIp && now - lastKeepalive > KEEPALIVE_MS) {
    lastKeepalive = now;
    OscOut hb;
    hb.str("/getHeartbeat");
    hb.str(",");
    oscSend(hb);
  }

  if (absetUp && now - lastResync > RESYNC_MS) {
    lastResync = now;
    sendResync();
  }

  if (wifiUp && !absetUp && now - lastSubscribe > RESUBSCRIBE_MS) {
    lastSubscribe = now;
    if (!haveAblesetIp || (uint32_t)abletonIp == 0) {
      haveAblesetIp = false;
      discoverAbleset();
    }
    sendSubscribe();
  }

  if (wifiUp && !absetUp &&
      now - wifiConnectedAt > ABLESET_HUNT_MS &&
      packetAgeMs() > ABLESET_HUNT_MS) {
    wifiUp = false;
    haveAblesetIp = false;
    connectedSsid = "";
    WiFi.disconnect(false, true);
    delay(100);
    connectWifi();
    lastWifiRetry = millis();
    if (gaveUp) return;
    gfx->fillScreen(COL_BG);
    forceRedraw = true;
    if (wifiUp) {
      discoverAbleset();
      sendSubscribe();
      lastSubscribe = millis();
    }
    now = millis();
    lastWifiCheck = now;
  }

  takeSnapshot();
  render();
  delay(10);
}