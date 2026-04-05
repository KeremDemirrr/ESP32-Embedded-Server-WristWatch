#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <ESP32Time.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>

// ==========================================
// KULLANICI AYARLARI (BURALARI DEĞİŞTİRİN)
// ==========================================
const char* ssid1 = "SSID_1";
const char* pass1 = "PASSWORD_1";
const char* ssid2 = "SSID_2";
const char* pass2 = "PASSWORD_2";
const char* ssid3 = "SSID_3";
const char* pass3 = "PASSWORD_3";

const char* AP_SSID    = "192.168.4.1";
const char* NOTES_FILE = "/notes.txt";

// Admin Paneli Ayarları
const char* ADMIN_PATH = "/admin";
const char* ADMIN_USER = "admin";
const char* ADMIN_PASS = "123456";
// ==========================================

#define PRG_BUTTON_PIN  0
#define HOLD_MS         5000

#define VEXT_PIN        36
#define OLED_SDA        17
#define OLED_SCL        18
#define OLED_RST        21
#define TOUCH_THRESHOLD 40

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL, GEOMETRY_128_64);
ESP32Time   rtc;
WebServer   server(80);
DNSServer   dnsServer;

const char* matrixChars = "0123456789ABCDEF!@#$%^&*";
static unsigned long prgPressStart = 0;
static bool          prgCounting   = false;

void drawLoadingScreen(String msg, int progress);
void drawWatchFace();
void syncTime();
void drawMatrixIntro();
bool checkPRGHold();
bool pollPRGHold();
void runHotspotMode();
int  countNotes();
void sendPage(String title, String body, bool adminPage = false);
void handleRoot();
void handleAbout();
void handleSubmit();
void handleAdmin();
void handleDownload();
void handleClear();
void handleCaptive();
void handleNotFound();

void gotTouch() {}

void setupDisplay() {
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20);
  digitalWrite(OLED_RST, HIGH); delay(20);
  display.init();
}

void drawLoadingScreen(String msg, int progress) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "INITIALIZING KERNEL...");
  display.drawLine(0, 12, 128, 12);
  display.drawString(0, 22, "> " + msg);
  display.drawRect(10, 48, 108, 6);
  display.fillRect(12, 50, (progress * 104) / 100, 2);
  display.display();
}

void drawMatrixIntro() {
  for (int i = 0; i < 10; i++) {
    display.clear();
    for (int col = 0; col < 128; col += 8) {
      char c = matrixChars[random(0, strlen(matrixChars))];
      display.drawString(col, (i * 8) % 64, String(c));
    }
    display.display();
    delay(25);
  }
}

void drawWatchFace() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "KERNEL-V3.2");
  display.drawLine(0, 12, 128, 12);
  display.fillRect(0, 14, 128, 30);
  display.setColor(BLACK);
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(55, 16, rtc.getTime("%H:%M"));
  display.setFont(ArialMT_Plain_10);
  display.drawString(105, 26, rtc.getTime("%S"));
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 46, rtc.getTime("%A, %d %b"));
  display.display();
}

void drawHotspotLoadingScreen() {
  for (int p = 0; p <= 100; p += 5) {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "HOTSPOT MODE");
    display.drawLine(0, 12, 128, 12);
    if (p < 30)       display.drawString(0, 22, "> AP baslatiliyor...");
    else if (p < 60)  display.drawString(0, 22, "> DNS sunucusu...");
    else if (p < 85)  display.drawString(0, 22, "> Web sunucusu...");
    else              display.drawString(0, 22, "> Hazir!");
    display.drawRect(10, 48, 108, 6);
    display.fillRect(12, 50, (p * 104) / 100, 2);
    display.display();
    delay(40);
  }
}

void drawHotspotScreen(int noteCount) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "[ HOTSPOT AKTIF ]");
  display.drawLine(0, 12, 128, 12);
  display.drawString(0, 16, "Baglan > ...");
  display.drawString(0, 28, "IP : 192.168.4.1");
  if (noteCount > 0)
    display.drawString(0, 40, "Not: " + String(noteCount) + " adet");
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 52, "5sn PRG -> cikis");
  display.display();
}

void syncTime() {
  const char* ssids[]  = {ssid1, ssid2, ssid3};
  const char* passes[] = {pass1, pass2, pass3};
  bool connected = false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  for (int i = 0; i < 3; i++) {
    if (String(ssids[i]) == "" || String(ssids[i]) == "SSID_1" || String(ssids[i]) == "SSID_2" || String(ssids[i]) == "SSID_3") continue;
    drawLoadingScreen("CONNECTING: " + String(ssids[i]), 20 + (i * 25));
    WiFi.begin(ssids[i], passes[i]);
    int counter = 0;
    while (WiFi.status() != WL_CONNECTED && counter < 20) {
      delay(500); counter++;
    }
    if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
    WiFi.disconnect(); delay(100);
  }

  if (connected) {
    drawLoadingScreen("RTC_CLOCK_SYNC", 85);
    configTime(10800, 0, "pool.ntp.org");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) rtc.setTimeStruct(timeinfo);
    drawLoadingScreen("KERNEL_READY", 100);
    delay(800);
  } else {
    drawLoadingScreen("ALL_NETS_OFFLINE", 100);
    delay(1200);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool checkPRGHold() {
  if (digitalRead(PRG_BUTTON_PIN) != LOW) return false;
  unsigned long pressStart = millis();
  while (digitalRead(PRG_BUTTON_PIN) == LOW) {
    unsigned long elapsed = millis() - pressStart;
    if (elapsed >= HOLD_MS) return true;
    int pct = (elapsed * 100) / HOLD_MS;
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 10, "[ HOTSPOT MODE ]");
    display.drawString(64, 26, "Basili tut...");
    display.drawRect(10, 44, 108, 8);
    display.fillRect(12, 46, (pct * 104) / 100, 4);
    display.display();
    delay(40);
  }
  return false;
}

bool pollPRGHold() {
  if (digitalRead(PRG_BUTTON_PIN) == LOW) {
    if (!prgCounting) {
      prgCounting   = true;
      prgPressStart = millis();
    }
    unsigned long elapsed = millis() - prgPressStart;
    if (elapsed >= HOLD_MS) { prgCounting = false; return true; }
    if (elapsed > 300) {
      int pct = (elapsed * 100) / HOLD_MS;
      display.clear();
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 10, "[ HOTSPOT MODE ]");
      display.drawString(64, 26, "Basili tut...");
      display.drawRect(10, 44, 108, 8);
      display.fillRect(12, 46, (pct * 104) / 100, 4);
      display.display();
    }
  } else {
    prgCounting = false;
  }
  return false;
}

int countNotes() {
  if (!LittleFS.exists(NOTES_FILE)) return 0;
  File f = LittleFS.open(NOTES_FILE, "r");
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.startsWith("---")) count++;
  }
  f.close();
  return count;
}

void sendPage(String title, String body, bool adminPage) {
  String html = "<!DOCTYPE html><html lang='tr'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>" + title + "</title>";

  html += F(R"CSS(<style>
:root{
  --bg:#0d1117;--bg2:#161b22;--border:#30363d;
  --text:#e6edf3;--muted:#8b949e;--accent:#238636;
  --accent-hover:#2ea043;--link:#58a6ff;
  --tag-bg:#388bfd1a;--tag-color:#58a6ff;--tag-border:#1f6feb;
  --warn-bg:#3d1f00;--warn-border:#bb4910;--warn-text:#f0883e;
  --danger:#da3633;--danger-hover:#b91c1c;--input-bg:#0d1117;
}
.light{
  --bg:#ffffff;--bg2:#f6f8fa;--border:#d0d7de;
  --text:#24292f;--muted:#57606a;--accent:#2da44e;
  --accent-hover:#2c974b;--link:#0969da;
  --tag-bg:#ddf4ff;--tag-color:#0550ae;--tag-border:#b6e3ff;
  --warn-bg:#fff8c5;--warn-border:#d4a72c;--warn-text:#633c01;
  --danger:#cf222e;--danger-hover:#a40e26;--input-bg:#ffffff;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif;
  font-size:14px;line-height:1.5;min-height:100vh;}
.topbar{background:var(--bg2);border-bottom:1px solid var(--border);
  padding:10px 16px;display:flex;align-items:center;gap:8px;position:sticky;top:0;z-index:10;}
.dots{display:flex;gap:5px;}
.dot{width:10px;height:10px;border-radius:50%;}
.topbar-title{color:var(--text);font-size:13px;font-weight:600;flex:1;}
.theme-btn{
  background:none;border:1px solid var(--border);border-radius:6px;
  color:var(--muted);font-size:14px;padding:2px 8px;cursor:pointer;
  font-family:inherit;transition:all .15s;line-height:1.5;font-weight:600;
}
.theme-btn:hover{border-color:var(--text);color:var(--text);}
.container{max-width:560px;margin:0 auto;padding:20px 16px;}
.card{background:var(--bg2);border:1px solid var(--border);border-radius:6px;overflow:hidden;margin-bottom:14px;}
.card-header{background:var(--bg);border-bottom:1px solid var(--border);
  padding:10px 14px;font-weight:600;font-size:14px;display:flex;align-items:center;gap:8px;}
.card-body{padding:14px;}
label{display:block;font-weight:600;font-size:13px;color:var(--text);margin-bottom:5px;margin-top:12px;}
label:first-child{margin-top:0}
input,textarea{
  width:100%;padding:5px 10px;font-size:14px;color:var(--text);
  background:var(--input-bg);border:1px solid var(--border);border-radius:6px;
  outline:none;font-family:inherit;transition:border-color .15s,box-shadow .15s;
}
input:focus,textarea:focus{border-color:var(--link);box-shadow:0 0 0 3px rgba(88,166,255,.1);}
textarea{height:110px;resize:vertical;}
input::placeholder,textarea::placeholder{color:var(--muted);}
.char-count{font-size:12px;color:var(--muted);text-align:right;margin-top:3px;}
.btn{
  display:inline-flex;align-items:center;gap:5px;
  padding:5px 14px;font-size:13px;font-weight:500;line-height:20px;
  border-radius:6px;border:1px solid var(--border);cursor:pointer;
  text-decoration:none;font-family:inherit;transition:background .15s,border-color .15s;
  color:var(--text);background:var(--bg2);
}
.btn:hover{background:var(--bg);border-color:var(--muted);}
.btn-primary{background:var(--accent);border-color:rgba(0,0,0,.2);color:#fff;}
.btn-primary:hover{background:var(--accent-hover);}
.btn-block{display:flex;width:100%;justify-content:center;margin-top:10px;}
.btn-danger{background:var(--danger);border-color:rgba(0,0,0,.2);color:#fff;}
.btn-danger:hover{background:var(--danger-hover);}
.muted{color:var(--muted);font-size:13px;}
.tag{display:inline-block;padding:1px 8px;font-size:11px;font-weight:500;
  border-radius:20px;background:var(--tag-bg);color:var(--tag-color);border:1px solid var(--tag-border);}
.note-item{border:1px solid var(--border);border-radius:6px;margin-bottom:8px;overflow:hidden;}
.note-meta{background:var(--bg);border-bottom:1px solid var(--border);
  padding:6px 12px;font-size:12px;color:var(--muted);display:flex;align-items:center;gap:5px;}
.note-meta strong{color:var(--text);}
.note-body{padding:10px 12px;font-size:14px;word-break:break-word;line-height:1.6;}
.confirm-box{display:none;background:var(--warn-bg);border:1px solid var(--warn-border);
  border-radius:6px;padding:10px 14px;margin-top:10px;font-size:13px;color:var(--warn-text);}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px;}
.link-box{background:var(--bg);border:1px solid var(--border);border-radius:6px;
  padding:8px 12px;font-family:ui-monospace,"SFMono-Regular",monospace;
  font-size:12px;color:var(--text);word-break:break-all;margin:8px 0;}
.stat{display:inline-flex;align-items:center;gap:4px;font-size:13px;color:var(--muted);margin-right:14px;}
.stat strong{color:var(--text);}
.divider{border:none;border-top:1px solid var(--border);margin:14px 0;}

/* Language Toggle CSS */
.en { display: none; }
body.en-mode .tr { display: none; }
body.en-mode span.en { display: inline; }
body.en-mode p.en, body.en-mode div.en { display: block; }
</style>)CSS");

  html += F(R"JS(<script>
function initSettings(){
  var c = document.cookie;
  var isLight = c.includes('theme=light');
  if(isLight) document.documentElement.classList.add('light');
  var tb = document.getElementById('themeBtn');
  if(tb) tb.innerHTML = isLight ? '&#128262;' : '&#127769;';

  var isEn = c.includes('lang=en');
  if(isEn) document.body.classList.add('en-mode');
  var lb = document.getElementById('langBtn');
  if(lb) lb.textContent = isEn ? 'EN' : 'TR';
  updatePlaceholders(isEn);
}
function toggleTheme(){
  var isLight = document.documentElement.classList.toggle('light');
  document.cookie = 'theme=' + (isLight ? 'light' : 'dark') + ';path=/;max-age=31536000';
  document.getElementById('themeBtn').innerHTML = isLight ? '&#128262;' : '&#127769;';
}
function toggleLang(){
  var isEn = document.body.classList.toggle('en-mode');
  document.cookie = 'lang=' + (isEn ? 'en' : 'tr') + ';path=/;max-age=31536000';
  document.getElementById('langBtn').textContent = isEn ? 'EN' : 'TR';
  updatePlaceholders(isEn);
}
function updatePlaceholders(isEn){
  var nm = document.getElementById('inpName');
  var nt = document.getElementById('inpNote');
  if(nm) nm.placeholder = isEn ? 'anonymous' : 'anonim';
  if(nt) nt.placeholder = isEn ? 'Write whatever is on your mind...' : 'Ne d\u00fc\u015f\u00fcn\u00fcyorsan yaz...';
}
</script>)JS");

  html += F("</head><body onload='initSettings()'>");

  html += F("<div class='topbar'>"
    "<div class='dots'>"
    "<div class='dot' style='background:#ff5f57'></div>"
    "<div class='dot' style='background:#ffbd2e'></div>"
    "<div class='dot' style='background:#28c840'></div>"
    "</div>"
    "<span class='topbar-title'>Embedded Server</span>"
    "<button class='theme-btn' id='langBtn' onclick='toggleLang()'>TR</button>"
    "<button class='theme-btn' id='themeBtn' onclick='toggleTheme()' style='font-size:16px;padding:1px 6px;'>&#127769;</button>"
    "</div>");

  html += "<div class='container'>" + body + "</div>";
  html += F("</body></html>");

  server.send(200, "text/html; charset=utf-8", html);
}

void handleCaptive() { handleRoot(); }
void handleNotFound() { handleRoot(); }

void handleRoot() {
  int nc = countNotes();
  String body = "";

  body += F("<div class='card'>"
    "<div class='card-header'>"
    "<svg width='14' height='14' viewBox='0 0 16 16' fill='currentColor'>"
    "<path d='M2 2.5A2.5 2.5 0 014.5 0h8.75a.75.75 0 01.75.75v12.5a.75.75 0 01-.75.75h-2.5a.75.75 0 110-1.5h1.75v-2h-8a1 1 0 00-.714 1.7.75.75 0 01-1.072 1.05A2.495 2.495 0 012 11.5v-9zm10.5-1V9h-8c-.356 0-.694.074-1 .208V2.5a1 1 0 011-1h8z'/>"
    "</svg>"
    "Embedded Server &nbsp;<span class='tag'>public</span>"
    "</div>"
    "<div class='card-body'>"
    "<p class='muted tr' style='margin-bottom:12px'>Bir &#351;eyler b&#305;rak. Kimse sormaz, kimse sorgulamaz.</p>"
    "<p class='muted en' style='margin-bottom:12px'>Leave something. No one asks, no one questions.</p>"
    "<div style='margin-bottom:14px'>");

  if (nc > 0) {
    body += "<span class='stat'>&#128196; <strong>" + String(nc) + "</strong> <span class='tr'>not</span><span class='en'>note(s)</span></span>";
  }
  body += F("<span class='stat'>&#128276; <span class='tr'>Hotspot aktif</span><span class='en'>Hotspot active</span></span></div>");
  body += F("<a href='/about' class='btn'><span class='tr'>Proje hakk&#305;nda</span><span class='en'>About project</span> &rarr;</a>");
  body += F("</div></div>");

  body += F("<div class='card'>"
    "<div class='card-header'>&#9998; <span class='tr'>Not b&#305;rak</span><span class='en'>Leave a note</span></div>"
    "<div class='card-body'>"
    "<form method='POST' action='/submit'>"
    "<label><span class='tr'>Takma ad&#305;n (opsiyonel)</span><span class='en'>Nickname (optional)</span></label>"
    "<input type='text' name='name' id='inpName' placeholder='anonim' maxlength='32' autocomplete='off'>"
    "<label><span class='tr'>Mesaj&#305;n</span><span class='en'>Your message</span></label>"
    "<textarea name='note' id='inpNote' placeholder='Ne d&#252;&#351;&#252;n&#252;yorsan yaz...' maxlength='500' required "
    "oninput=\"document.getElementById('cc').textContent=this.value.length+'/500'\"></textarea>"
    "<div class='char-count'><span id='cc'>0/500</span></div>"
    "<button type='submit' class='btn btn-primary btn-block'><span class='tr'>G&#246;nder</span><span class='en'>Submit</span></button>"
    "</form>"
    "</div></div>");

  body += F("<p class='muted' style='text-align:center;margin-top:4px'>kernel-v3.2 &mdash; ESP32 Heltec LoRa V3</p>");

  sendPage("Embedded Server", body);
}

void handleAbout() {
  String body = "";

  body += F("<div class='card'>"
    "<div class='card-header'>"
    "<svg width='14' height='14' viewBox='0 0 16 16' fill='currentColor'>"
    "<path d='M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z'/>"
    "</svg>"
    "<span class='tr'>Embedded Server &mdash; Proje Hakk&#305;nda</span>"
    "<span class='en'>Embedded Server &mdash; About Project</span>"
    "</div>"
    "<div class='card-body'>");

  body += F("<p class='tr' style='margin-bottom:14px;line-height:1.7'>"
    "Bu proje, bir Heltec WiFi LoRa 32 V3.2 geli&#351;tirme kart&#305;n&#305; "
    "tam i&#351;levli bir kol saatine d&ouml;n&uuml;&#351;t&uuml;rme fikriyle ba&#351;lad&#305;. "
    "Sadece zaman g&ouml;stermekle kalmay&#305;p kendi i&ccedil;inde bir web sunucusu bar&#305;nd&#305;ran, "
    "ba&#351;ka cihazlarla kablosuz ileti&#351;im kurabilen bir sistem hedeflendi."
    "</p>");
  body += F("<p class='en' style='margin-bottom:14px;line-height:1.7'>"
    "This project started with the idea of turning a Heltec WiFi LoRa 32 V3.2 development board into a fully functional wristwatch. "
    "It aims to be a system that not only shows time but also hosts an internal web server and can communicate wirelessly with other devices."
    "</p>");

  body += F("<hr class='divider'>"
    "<label style='margin-top:0'>&#9881; <span class='tr'>Donan&#305;m</span><span class='en'>Hardware</span></label>"
    "<p class='muted tr' style='line-height:1.7'>"
    "Heltec WiFi LoRa 32 V3.2 &mdash; ESP32-S3 tabanl&#305;, 128&times;64 piksel OLED ekran, "
    "LoRa 868MHz radyo mod&uuml;l&uuml;, kapasitif dokunmatik pinler ve dahili RTC. "
    "T&uuml;m sistem 3.7V LiPo pil ile besleniyor. "
    "Deep sleep modu sayesinde pil &ouml;mr&uuml; ciddi &ouml;l&ccedil;&uuml;de uzat&#305;ld&#305;."
    "</p>"
    "<p class='muted en' style='line-height:1.7'>"
    "Heltec WiFi LoRa 32 V3.2 &mdash; ESP32-S3 based, 128&times;64 pixel OLED screen, "
    "LoRa 868MHz radio module, capacitive touch pins, and built-in RTC. "
    "The entire system is powered by a 3.7V LiPo battery. "
    "Battery life has been significantly extended thanks to the deep sleep mode."
    "</p>");

  body += F("<hr class='divider'>"
    "<label>&#128196; <span class='tr'>Yaz&#305;l&#305;m Mimarisi</span><span class='en'>Software Architecture</span></label>"
    "<p class='muted tr' style='line-height:1.7'>"
    "Arduino framework &uuml;zerinde geli&#351;tirildi. "
    "Sistem iki ana modda &ccedil;al&#305;&#351;&#305;yor: normal saat modu ve hotspot modu. "
    "Normal modda NTP &uuml;zerinden saat senkronizasyonu yap&#305;l&#305;p deep sleep'e giriliyor, "
    "dokunmatik pinlerle uyand&#305;r&#305;l&#305;yor. "
    "Hotspot modunda ise ESP32 kendi &uuml;zerinde bir DNS sunucusu ve HTTP web sunucusu "
    "a&#231;arak ba&#287;lanan cihazlara sayfa servisi yap&#305;yor. "
    "Notlar LittleFS dosya sisteminde saklan&#305;yor, yeniden ba&#351;latmalar aras&#305;nda korunuyor."
    "</p>"
    "<p class='muted en' style='line-height:1.7'>"
    "Developed on the Arduino framework. The system operates in two main modes: normal watch mode and hotspot mode. "
    "In normal mode, it syncs time via NTP, enters deep sleep, and wakes up via touch pins. "
    "In hotspot mode, the ESP32 opens its own DNS and HTTP web server, serving pages to connected devices. "
    "Notes are stored in the LittleFS file system, preserving them across reboots."
    "</p>");

  body += F("<hr class='divider'>"
    "<label>&#10024; <span class='tr'>&Ouml;zellikler</span><span class='en'>Features</span></label>"
    "<p class='muted tr' style='margin-bottom:5px'>&#8226; &nbsp;NTP senkronizasyonu &mdash; GMT+3, 3 farkl&#305; a&#287; denenebiliyor</p>"
    "<p class='muted en' style='margin-bottom:5px'>&#8226; &nbsp;NTP synchronization &mdash; GMT+3, tries 3 different networks</p>"
    "<p class='muted tr' style='margin-bottom:5px'>&#8226; &nbsp;Kapasitif dokunmatik uyand&#305;rma &mdash; T1/T2/T3/T7 pinleri</p>"
    "<p class='muted en' style='margin-bottom:5px'>&#8226; &nbsp;Capacitive touch wake-up &mdash; T1/T2/T3/T7 pins</p>"
    "<p class='muted tr' style='margin-bottom:5px'>&#8226; &nbsp;Deep sleep &mdash; ekran kararma + uyku d&ouml;ng&uuml;s&uuml;</p>"
    "<p class='muted en' style='margin-bottom:5px'>&#8226; &nbsp;Deep sleep &mdash; screen dimming + sleep cycle</p>"
    "<p class='muted tr' style='margin-bottom:5px'>&#8226; &nbsp;&#350;ifresiz hotspot &mdash; captive portal + DNS</p>"
    "<p class='muted en' style='margin-bottom:5px'>&#8226; &nbsp;Open hotspot &mdash; captive portal + DNS</p>"
    "<p class='muted tr' style='margin-bottom:5px'>&#8226; &nbsp;LittleFS not saklama &mdash; flash bellek, 500 karakter limit</p>"
    "<p class='muted en' style='margin-bottom:5px'>&#8226; &nbsp;LittleFS note storage &mdash; flash memory, 500 char limit</p>"
    "<p class='muted tr'>&#8226; &nbsp;Koyu / a&#231;&#305;k tema &mdash; cookie ile sayfalar aras&#305; kal&#305;c&#305;</p>"
    "<p class='muted en'>&#8226; &nbsp;Dark / light theme &mdash; persistent via cookies</p>");

  body += F("<hr class='divider'>"
    "<label>&#128279; GitHub</label>"
    "<div class='link-box'>https://github.com/KeremDemirrr/ESP32-Embedded-Server-WristWatch</div>"
    "<p class='muted tr' style='font-size:12px'>"
    "Kaynak kodu, devre &#351;emas&#305; ve kurulum ad&#305;mlar&#305; i&ccedil;in linki kopyalay&#305;p "
    "internete ba&#287;land&#305;ktan sonra ziyaret edebilirsiniz."
    "</p>"
    "<p class='muted en' style='font-size:12px'>"
    "For source code, circuit schematics, and installation steps, copy the link and visit it once connected to the internet."
    "</p>");

  body += F("<hr class='divider'>"
    "<a href='/' class='btn'>&#8592; <span class='tr'>Geri d&ouml;n</span><span class='en'>Go back</span></a>"
    "</div></div>");

  sendPage("Proje Hakkinda", body);
}

void handleSubmit() {
  if (server.method() != HTTP_POST) {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  String name = server.arg("name");
  String note = server.arg("note");
  name.trim(); note.trim();
  if (name.length() == 0) name = "anonim";

  if (note.length() > 0 && note.length() <= 500) {
    File f = LittleFS.open(NOTES_FILE, "a");
    if (f) {
      f.println("---");
      f.println("FROM: " + name);
      f.println("NOTE: " + note);
      f.close();
    }
  }

  String body = F("<div class='card'>"
    "<div class='card-header'>&#10003; <span class='tr'>Kaydedildi</span><span class='en'>Saved</span></div>"
    "<div class='card-body'>"
    "<p class='muted tr' style='margin-bottom:14px'>Mesaj&#305;n sisteme yaz&#305;ld&#305;.</p>"
    "<p class='muted en' style='margin-bottom:14px'>Your message has been written to the system.</p>"
    "<a href='/' class='btn btn-primary'>&#8592; <span class='tr'>Geri d&ouml;n</span><span class='en'>Go back</span></a>"
    "</div></div>");

  sendPage("Kaydedildi", body);
}

void handleAdmin() {
  if (server.method() == HTTP_GET) {
    String body = F("<div class='card'>"
      "<div class='card-header'>&#128274; <span class='tr'>Admin Giri&#351;i</span><span class='en'>Admin Login</span></div>"
      "<div class='card-body'>"
      "<form method='POST' action='");
    body += String(ADMIN_PATH);
    body += F("'>"
      "<label><span class='tr'>Kullan&#305;c&#305; ad&#305;</span><span class='en'>Username</span></label>"
      "<input type='text' name='u' autocomplete='off'>"
      "<label><span class='tr'>&#350;ifre</span><span class='en'>Password</span></label>"
      "<input type='password' name='p'>"
      "<button type='submit' class='btn btn-primary btn-block'><span class='tr'>Giri&#351; Yap</span><span class='en'>Login</span></button>"
      "</form>"
      "</div></div>");
    sendPage("Admin", body);
    return;
  }

  String u = server.arg("u");
  String p = server.arg("p");
  u.trim(); p.trim();

  if (u != String(ADMIN_USER) || p != String(ADMIN_PASS)) {
    String body = F("<div class='card'>"
      "<div class='card-header' style='color:#f85149'>&#10007; <span class='tr'>Hatal&#305; giri&#351;</span><span class='en'>Login Failed</span></div>"
      "<div class='card-body'>"
      "<p class='muted tr' style='margin-bottom:12px'>Kullan&#305;c&#305; ad&#305; veya &#351;ifre yanl&#305;&#351;.</p>"
      "<p class='muted en' style='margin-bottom:12px'>Incorrect username or password.</p>"
      "<a href='");
    body += String(ADMIN_PATH);
    body += F("' class='btn'>&#8592; <span class='tr'>Tekrar dene</span><span class='en'>Try again</span></a>"
      "</div></div>");
    sendPage("Hata", body);
    return;
  }

  String rawContent = "";
  if (LittleFS.exists(NOTES_FILE)) {
    File f = LittleFS.open(NOTES_FILE, "r");
    if (f) { while (f.available()) rawContent += (char)f.read(); f.close(); }
  }

  String notesHtml = "";
  int noteCount = 0;

  if (rawContent.length() == 0) {
    notesHtml = "<p class='muted'><span class='tr'>Hen&uuml;z not yok.</span><span class='en'>No notes yet.</span></p>";
  } else {
    String from = "anonim", note = "";
    bool inNote = false;
    int lineStart = 0;

    for (int i = 0; i <= (int)rawContent.length(); i++) {
      if (i == (int)rawContent.length() || rawContent[i] == '\n') {
        String line = rawContent.substring(lineStart, i);
        line.trim();
        lineStart = i + 1;

        if (line == "---") {
          if (inNote && note.length() > 0) {
            noteCount++;
            notesHtml += "<div class='note-item'><div class='note-meta'>&#128100; <strong>"
              + from + "</strong> &mdash; #" + String(noteCount) + "</div>"
              + "<div class='note-body'>" + note + "</div></div>";
          }
          from = "anonim"; note = ""; inNote = true;
        } else if (line.startsWith("FROM: ")) {
          from = line.substring(6);
        } else if (line.startsWith("NOTE: ")) {
          note = line.substring(6);
        }
      }
    }
    if (inNote && note.length() > 0) {
      noteCount++;
      notesHtml += "<div class='note-item'><div class='note-meta'>&#128100; <strong>"
        + from + "</strong> &mdash; #" + String(noteCount) + "</div>"
        + "<div class='note-body'>" + note + "</div></div>";
    }
    if (noteCount == 0) notesHtml = "<p class='muted'><span class='tr'>Hen&uuml;z not yok.</span><span class='en'>No notes yet.</span></p>";
  }

  String body = "<div class='card'><div class='card-header'>&#128274; Admin Panel &mdash; ";
  if (noteCount > 0) {
    body += String(noteCount) + " <span class='tr'>not</span><span class='en'>note(s)</span>";
  } else {
    body += "<span class='tr'>Not yok</span><span class='en'>No notes</span>";
  }
  body += F("</div><div class='card-body'>");
  body += notesHtml;
  body += F("<div class='actions'>"
    "<a class='btn' href='/download'>&#8595; <span class='tr'>.txt indir</span><span class='en'>Download .txt</span></a>"
    "<button class='btn btn-danger' onclick=\"document.getElementById('cf').style.display='block'\">&#128465; <span class='tr'>Hepsini sil</span><span class='en'>Delete all</span></button>"
    "</div>"
    "<div class='confirm-box' id='cf'>"
    "&#9888; <span class='tr'>Emin misin? Bu i&#351;lem geri al&#305;namaz.</span><span class='en'>Are you sure? This action cannot be undone.</span><br><br>"
    "<a href='/clear' class='btn btn-danger' style='margin-right:8px'><span class='tr'>Evet, sil</span><span class='en'>Yes, delete</span></a>"
    "<button class='btn' onclick=\"document.getElementById('cf').style.display='none'\">&#215; <span class='tr'>&#304;ptal</span><span class='en'>Cancel</span></button>"
    "</div></div></div>");

  sendPage("Admin Panel", body, true);
}

void handleDownload() {
  if (LittleFS.exists(NOTES_FILE)) {
    File f = LittleFS.open(NOTES_FILE, "r");
    if (f) {
      server.sendHeader("Content-Disposition", "attachment; filename=notlar.txt");
      server.streamFile(f, "text/plain; charset=utf-8");
      f.close();
      return;
    }
  }
  server.send(200, "text/plain; charset=utf-8", "Henuz not yok. / No notes yet.\n");
}

void handleClear() {
  if (LittleFS.exists(NOTES_FILE)) LittleFS.remove(NOTES_FILE);
  server.sendHeader("Location", ADMIN_PATH);
  server.send(302, "text/plain", "");
}

void runHotspotMode() {
  if (!LittleFS.begin(true)) {}

  drawHotspotLoadingScreen();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  delay(2000);

  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/generate_204",          HTTP_GET,  handleCaptive);
  server.on("/gen_204",               HTTP_GET,  handleCaptive);
  server.on("/hotspot-detect.html",   HTTP_GET,  handleCaptive);
  server.on("/library/test/success.html", HTTP_GET,  handleCaptive);
  server.on("/success.txt",           HTTP_GET,  handleCaptive);
  server.on("/ncsi.txt",              HTTP_GET,  handleCaptive);
  server.on("/connecttest.txt",       HTTP_GET,  handleCaptive);
  server.on("/redirect",              HTTP_GET,  handleCaptive);
  server.on("/canonical.html",        HTTP_GET,  handleCaptive);

  server.on("/",         HTTP_GET,  handleRoot);
  server.on("/about",    HTTP_GET,  handleAbout);
  server.on("/submit",   HTTP_POST, handleSubmit);
  server.on(ADMIN_PATH,  HTTP_GET,  handleAdmin);
  server.on(ADMIN_PATH,  HTTP_POST, handleAdmin);
  server.on("/download", HTTP_GET,  handleDownload);
  server.on("/clear",    HTTP_GET,  handleClear);
  server.onNotFound(handleNotFound);
  server.begin();

  display.setBrightness(200);
  unsigned long lastDraw = 0;
  prgCounting = false;

  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();

    if (millis() - lastDraw > 2000) {
      drawHotspotScreen(countNotes());
      lastDraw = millis();
    }

    if (digitalRead(PRG_BUTTON_PIN) == LOW) {
      if (!prgCounting) { prgCounting = true; prgPressStart = millis(); }
      unsigned long elapsed = millis() - prgPressStart;
      if (elapsed >= HOLD_MS) { prgCounting = false; break; }
      if (elapsed > 300) {
        int pct = (elapsed * 100) / HOLD_MS;
        display.clear();
        display.setFont(ArialMT_Plain_10);
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 10, "[ CIKIS ]");
        display.drawString(64, 26, "Basili tut...");
        display.drawRect(10, 44, 108, 8);
        display.fillRect(12, 46, (pct * 104) / 100, 4);
        display.display();
      }
    } else {
      if (prgCounting) {
        prgCounting = false;
        drawHotspotScreen(countNotes());
        lastDraw = millis();
      }
    }
    delay(10);
  }

  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  LittleFS.end();
}

void setup() {
  setupDisplay();
  pinMode(PRG_BUTTON_PIN, INPUT_PULLUP);

  touchSleepWakeUpEnable(T1, TOUCH_THRESHOLD);
  touchSleepWakeUpEnable(T2, TOUCH_THRESHOLD);
  touchSleepWakeUpEnable(T3, TOUCH_THRESHOLD);
  touchSleepWakeUpEnable(T7, TOUCH_THRESHOLD);
  touchAttachInterrupt(T1, gotTouch, TOUCH_THRESHOLD);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  configTime(10800, 0, "pool.ntp.org");
  display.setBrightness(200);

  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    if (checkPRGHold()) {
      runHotspotMode();
    } else {
      syncTime();
      drawMatrixIntro();
    }
  } else {
    drawMatrixIntro();
  }

  display.setBrightness(255);
  unsigned long startFull = millis();
  while (millis() - startFull < 6000) {
    if (pollPRGHold()) {
      display.setBrightness(200);
      runHotspotMode();
      display.setBrightness(255);
      startFull = millis();
    }
    if (!prgCounting) drawWatchFace();
    delay(100);
  }

  display.setBrightness(1);
  unsigned long startDim = millis();
  while (millis() - startDim < 30000) {
    if (pollPRGHold()) {
      display.setBrightness(200);
      runHotspotMode();
      display.setBrightness(1);
      startDim = millis();
    }
    if (!prgCounting) drawWatchFace();
    delay(100);
  }

  display.clear();
  display.display();
  digitalWrite(VEXT_PIN, HIGH);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_deep_sleep_start();
}

void loop() {}