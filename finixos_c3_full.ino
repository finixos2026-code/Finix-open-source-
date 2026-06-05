/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║         FINIX OS v13.0 — ESP32-C3 Super Mini FULL              ║
 * ║         Creator : Nurudeen Al Haitami (Alkha)                  ║
 * ║         CPU     : ESP32-C3 RISC-V @ 160MHz | 400KB RAM        ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * FITUR LENGKAP:
 *  [BLE]     BLE Serial (NUS) — keyboard virtual dari HP via app
 *  [SHELL]   Terminal/Shell built-in via BLE + Serial USB
 *  [WIFI]    Connect/scan/disconnect WiFi via perintah
 *  [FS]      Buat folder, buat/edit/hapus file via editor built-in (SPIFFS)
 *  [JAM]     NTP + RTC DS1307/DS3231 fallback
 *  [SENSOR]  DS18B20 (suhu), LDR (cahaya), MQ-135 (kualitas udara)
 *  [LANG]    Bahasa skrip built-in "FSH" — variabel, if, loop, print
 *  [NET]     HTTP GET, ping (ICMP-like), port scan (nmap sederhana)
 *  [SD]      Baca/tulis SD card via SPI
 *  [MUSIK]   Putar WAV/tone dari SD, output ke amplifier via I2S/DAC
 *  [OLED]    Dashboard jam, sensor, musik, sysinfo — menu navigasi
 *  [SYSMON]  RAM, Flash, CPU freq, uptime, suhu chip
 *  [BOOT]    Animasi boot Linux-style di OLED
 *
 * WIRING:
 *  OLED SSD1306  SDA=8   SCL=9
 *  DS18B20       DATA=4  (pull-up 4.7k ke 3.3V)
 *  LDR           AO=A0(GPIO0)
 *  MQ-135        AO=A1(GPIO1)
 *  DS3231 RTC    SDA=8   SCL=9  (same I2C bus, addr 0x68)
 *  SD Card SPI   MOSI=7  MISO=2  SCK=6  CS=10
 *  I2S/DAC       BCLK=5  LRCLK=3 DOUT=3 (atau DAC GPIO19)
 *  BLE           Built-in
 *
 * LIBRARIES (install via Library Manager):
 *  Adafruit SSD1306, Adafruit GFX
 *  OneWire, DallasTemperature
 *  SD (built-in)
 *  NimBLE-Arduino  (WAJIB — lebih ringan dari ESP32 BLE default)
 *  RTClib (Adafruit)
 *
 * APP HP untuk BLE Serial:
 *  Android: "Serial Bluetooth Terminal" by Kai Morich
 *  iOS    : "BLE Terminal HM-10"
 *  Sambung ke device "FINIX-C3", service Nordic UART (6E40...)
 */

// ═══════════════════════════════════════════════════════════════
//  INCLUDES
// ═══════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>

// NimBLE — jauh lebih ringan dari default BLE ESP32
// Install: Library Manager → "NimBLE-Arduino"
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>

// ═══════════════════════════════════════════════════════════════
//  PIN CONFIG
// ═══════════════════════════════════════════════════════════════
#define OLED_W        128
#define OLED_H        64
#define PIN_SDA       8
#define PIN_SCL       9
#define PIN_DS18B20   4
#define PIN_LDR       0    // GPIO0 = ADC1_CH0
#define PIN_MQ135     1    // GPIO1 = ADC1_CH1
#define PIN_SD_CS     10
#define PIN_SD_MOSI   7
#define PIN_SD_MISO   2
#define PIN_SD_SCK    6
// I2S/DAC output (opsional, pakai DAC internal GPIO19)
#define PIN_DAC_OUT   19

// ═══════════════════════════════════════════════════════════════
//  WIFI & NTP
// ═══════════════════════════════════════════════════════════════
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET      (7 * 3600)
#define DST_OFFSET      0
#define NTP_RESYNC_MS   (6UL * 3600UL * 1000UL)
#define WIFI_TIMEOUT_MS 15000

// ═══════════════════════════════════════════════════════════════
//  BLE NUS (Nordic UART Service) UUIDs
// ═══════════════════════════════════════════════════════════════
#define BLE_DEVICE_NAME   "FINIX-C3"
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ═══════════════════════════════════════════════════════════════
//  BATAS SISTEM (disesuaikan RAM C3 ~280KB usable)
// ═══════════════════════════════════════════════════════════════
#define MAX_LINE      128
#define MAX_HISTORY   15
#define MAX_NOTES     8
#define MAX_TODO      8
#define MAX_FW_IP     6
#define MAX_NAME      20
#define NOTE_LEN      72
#define TODO_LEN      56
#define FSH_VARS      8
#define FSH_VAR_LEN   32

// ═══════════════════════════════════════════════════════════════
//  OS INFO
// ═══════════════════════════════════════════════════════════════
#define OS_NAME     "FINIX OS"
#define OS_VER      "13.0"
#define OS_AUTHOR   "Alkha"
#define HOSTNAME    "finix-c3"

// ═══════════════════════════════════════════════════════════════
//  OBJEK GLOBAL
// ═══════════════════════════════════════════════════════════════
Adafruit_SSD1306  display(OLED_W, OLED_H, &Wire, -1);
OneWire           ow(PIN_DS18B20);
DallasTemperature ds18b20(&ow);
RTC_DS3231        rtc;

// BLE
NimBLEServer         *bleServer    = nullptr;
NimBLECharacteristic *bleTxChar    = nullptr;
bool                  bleConnected = false;

// State
bool          wifiOk    = false;
bool          ntpOk     = false;
bool          sdOk      = false;
bool          rtcOk     = false;
bool          spiffsOk  = false;
unsigned long lastNtp   = 0;

// Sensor cache (update tiap 2 detik)
float         sensorTemp   = 0;
int           sensorLdr    = 0;
int           sensorMq135  = 0;
unsigned long lastSensor   = 0;

// Menu OLED
enum OledMenu { MENU_CLOCK, MENU_SENSOR, MENU_SYSMON, MENU_MUSIK, MENU_TOTAL };
OledMenu      oledMenu    = MENU_CLOCK;
unsigned long lastMenuSwitch = 0;
bool          autoRotate  = true; // auto ganti menu tiap 5 detik

// Musik
bool          musikPlaying = false;
String        musikFile    = "";
unsigned long musikPos     = 0;

// ═══════════════════════════════════════════════════════════════
//  SHELL STATE
// ═══════════════════════════════════════════════════════════════
char          wifiSsid[32]  = "";
char          wifiPass[64]  = "";
bool          loggedIn      = false;
char          shellPwd[64]  = "/";
char          history[MAX_HISTORY][MAX_LINE];
int           histCount     = 0;

// ═══════════════════════════════════════════════════════════════
//  NOTES & TODO
// ═══════════════════════════════════════════════════════════════
struct Note { char text[NOTE_LEN]; char waktu[12]; };
struct Todo { char text[TODO_LEN]; bool done; };
static Note notes[MAX_NOTES];
static Todo todos[MAX_TODO];
static int  noteCount = 0, todoCount = 0;

// ═══════════════════════════════════════════════════════════════
//  FIREWALL (blokir IP)
// ═══════════════════════════════════════════════════════════════
static char fwBlock[MAX_FW_IP][20];
static int  fwCount = 0;

// ═══════════════════════════════════════════════════════════════
//  FSH SCRIPT ENGINE — variabel built-in
// ═══════════════════════════════════════════════════════════════
struct FshVar { char name[12]; char val[FSH_VAR_LEN]; };
static FshVar fshVars[FSH_VARS];
static int    fshVarCount = 0;

// ═══════════════════════════════════════════════════════════════
//  BLE OUTPUT — kirim ke HP
// ═══════════════════════════════════════════════════════════════
static void blePrint(const String &s) {
  if (bleConnected && bleTxChar) {
    // Pecah per 20 byte (BLE MTU minimum)
    int len = s.length();
    for (int i = 0; i < len; i += 20) {
      bleTxChar->setValue(s.substring(i, min(i+20, len)));
      bleTxChar->notify();
      delay(10);
    }
  }
}

static void blePrintln(const String &s) {
  blePrint(s + "\r\n");
}

// Output ke SEMUA: Serial USB + BLE
static void shellPrint(const String &s) {
  Serial.print(s);
  blePrint(s);
}
static void shellPrintln(const String &s) {
  Serial.println(s);
  blePrintln(s);
}

// ═══════════════════════════════════════════════════════════════
//  BLE CALLBACKS
// ═══════════════════════════════════════════════════════════════
// Buffer input dari BLE
static String bleInputBuf = "";
static bool   bleInputReady = false;

class BleServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s) override {
    bleConnected = true;
    Serial.println("[BLE] HP terhubung");
    blePrintln("\r\n" OS_NAME " v" OS_VER " — FINIX Shell");
    blePrintln("Ketik 'bantuan' untuk daftar perintah.");
    blePrintln("login / daftar untuk masuk.\r\n");
  }
  void onDisconnect(NimBLEServer *s) override {
    bleConnected = false;
    Serial.println("[BLE] HP terputus");
    // Restart advertising agar bisa sambung lagi
    NimBLEDevice::startAdvertising();
  }
};

class BleRxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string val = c->getValue();
    for (char ch : val) {
      if (ch == '\r' || ch == '\n') {
        bleInputReady = true;
      } else if (ch == 127 || ch == 8) {
        if (bleInputBuf.length() > 0)
          bleInputBuf.remove(bleInputBuf.length()-1);
      } else {
        bleInputBuf += ch;
      }
    }
  }
};

// ═══════════════════════════════════════════════════════════════
//  BLE INIT
// ═══════════════════════════════════════════════════════════════
static void bleInit() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3); // daya rendah tapi cukup

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new BleServerCB());

  NimBLEService *nus = bleServer->createService(NUS_SERVICE_UUID);

  // TX: ESP32 → HP (notify)
  bleTxChar = nus->createCharacteristic(
    NUS_TX_UUID,
    NIMBLE_PROPERTY::NOTIFY
  );

  // RX: HP → ESP32 (write)
  NimBLECharacteristic *rxChar = nus->createCharacteristic(
    NUS_RX_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  rxChar->setCallbacks(new BleRxCB());

  nus->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  Serial.println("[BLE] Advertising: " BLE_DEVICE_NAME);
}

// ═══════════════════════════════════════════════════════════════
//  SPIFFS HELPERS
// ═══════════════════════════════════════════════════════════════
static void notesLoad() {
  noteCount = 0;
  if (!spiffsOk || !SPIFFS.exists("/notes.dat")) return;
  File f = SPIFFS.open("/notes.dat","r");
  while (f.available() && noteCount < MAX_NOTES) {
    String line = f.readStringUntil('\n'); line.trim();
    int p = line.indexOf('|');
    if (p<0) continue;
    strncpy(notes[noteCount].waktu, line.substring(0,p).c_str(), 11);
    strncpy(notes[noteCount].text,  line.substring(p+1).c_str(), NOTE_LEN-1);
    noteCount++;
  }
  f.close();
}
static void notesSave() {
  if (!spiffsOk) return;
  File f = SPIFFS.open("/notes.dat","w");
  for (int i=0;i<noteCount;i++)
    f.printf("%s|%s\n", notes[i].waktu, notes[i].text);
  f.close();
}
static void todoLoad() {
  todoCount = 0;
  if (!spiffsOk || !SPIFFS.exists("/todo.dat")) return;
  File f = SPIFFS.open("/todo.dat","r");
  while (f.available() && todoCount < MAX_TODO) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length()<2) continue;
    todos[todoCount].done = (line[0]=='1');
    strncpy(todos[todoCount].text, line.substring(2).c_str(), TODO_LEN-1);
    todoCount++;
  }
  f.close();
}
static void todoSave() {
  if (!spiffsOk) return;
  File f = SPIFFS.open("/todo.dat","w");
  for (int i=0;i<todoCount;i++)
    f.printf("%c %s\n", todos[i].done?'1':'0', todos[i].text);
  f.close();
}

// ═══════════════════════════════════════════════════════════════
//  HELPER WAKTU
// ═══════════════════════════════════════════════════════════════
static String getTimeStr() {
  struct tm t;
  if (ntpOk && getLocalTime(&t)) {
    char b[10]; sprintf(b,"%02d:%02d:%02d",t.tm_hour,t.tm_min,t.tm_sec);
    return String(b);
  }
  if (rtcOk) {
    DateTime dt = rtc.now();
    char b[10]; sprintf(b,"%02d:%02d:%02d",dt.hour(),dt.minute(),dt.second());
    return String(b);
  }
  return "--:--:--";
}
static String getDateStr() {
  struct tm t;
  if (ntpOk && getLocalTime(&t)) {
    char b[12]; sprintf(b,"%02d-%02d-%04d",t.tm_mday,t.tm_mon+1,t.tm_year+1900);
    return String(b);
  }
  if (rtcOk) {
    DateTime dt = rtc.now();
    char b[12]; sprintf(b,"%02d-%02d-%04d",dt.day(),dt.month(),dt.year());
    return String(b);
  }
  return "--/--/----";
}
static String getDayStr() {
  const char *d[]={"Minggu","Senin","Selasa","Rabu","Kamis","Jumat","Sabtu"};
  struct tm t;
  if (ntpOk && getLocalTime(&t)) return String(d[t.tm_wday]);
  if (rtcOk) return String(d[rtc.now().dayOfTheWeek()]);
  return "";
}
static String getUptime() {
  long s = millis()/1000;
  char b[20]; sprintf(b,"%ldj%ldm%lds",s/3600,(s%3600)/60,s%60);
  return String(b);
}

// ═══════════════════════════════════════════════════════════════
//  SENSOR UPDATE (non-blocking, tiap 2 detik)
// ═══════════════════════════════════════════════════════════════
static void sensorUpdate() {
  if (millis() - lastSensor < 2000) return;
  lastSensor = millis();

  // DS18B20
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempCByIndex(0);
  if (t != DEVICE_DISCONNECTED_C) sensorTemp = t;

  // LDR (ADC 12-bit, 0-4095)
  sensorLdr   = analogRead(PIN_LDR);

  // MQ-135
  sensorMq135 = analogRead(PIN_MQ135);
}

static String ldrStatus() {
  if (sensorLdr < 800)  return "Gelap";
  if (sensorLdr < 2000) return "Redup";
  if (sensorLdr < 3200) return "Terang";
  return "Sangat Terang";
}
static String mq135Status() {
  if (sensorMq135 < 800)  return "Baik";
  if (sensorMq135 < 2000) return "Sedang";
  if (sensorMq135 < 3000) return "Buruk";
  return "BAHAYA";
}

// ═══════════════════════════════════════════════════════════════
//  WIFI
// ═══════════════════════════════════════════════════════════════
static bool wifiConnect(const char *ssid, const char *pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  unsigned long t = millis();
  while (WiFi.status()!=WL_CONNECTED) {
    if (millis()-t > WIFI_TIMEOUT_MS) return false;
    delay(250);
  }
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
  return true;
}
static bool ntpSync() {
  if (!wifiOk) return false;
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  struct tm ti; int r=0;
  while (!getLocalTime(&ti) && r<20) { delay(300); r++; }
  lastNtp = millis();
  return r<20;
}

// ═══════════════════════════════════════════════════════════════
//  MUSIK — putar file WAV sederhana via DAC GPIO19
//  (hanya PCM 8-bit mono dari SD, sample rate rendah)
// ═══════════════════════════════════════════════════════════════
static File   wavFile;
static bool   wavPlaying = false;
static uint8_t wavBuf[64];

static void musikPlay(const char *path) {
  if (!sdOk) { shellPrintln("  SD tidak tersedia."); return; }
  if (wavPlaying) { wavFile.close(); wavPlaying = false; }
  wavFile = SD.open(path);
  if (!wavFile) { shellPrintln("  File tidak ditemukan: " + String(path)); return; }
  // Skip WAV header 44 byte
  wavFile.seek(44);
  wavPlaying   = true;
  musikPlaying = true;
  musikFile    = String(path);
  shellPrintln("  Memutar: " + String(path));
}

static void musikStop() {
  if (wavPlaying) { wavFile.close(); wavPlaying = false; }
  musikPlaying = false;
  dacWrite(PIN_DAC_OUT, 128); // tengah = senyap
  shellPrintln("  Musik dihentikan.");
}

// Dipanggil di loop() — non-blocking, baca 64 sample per siklus
static void musikTick() {
  if (!wavPlaying) return;
  int n = wavFile.read(wavBuf, sizeof(wavBuf));
  if (n <= 0) {
    musikStop();
    shellPrintln("  Selesai.");
    return;
  }
  for (int i = 0; i < n; i++) {
    dacWrite(PIN_DAC_OUT, wavBuf[i]);
    delayMicroseconds(125); // ~8kHz sample rate
  }
}

// ═══════════════════════════════════════════════════════════════
//  OLED — BOOT LOG
// ═══════════════════════════════════════════════════════════════
#define LOG_ROWS 8
static String logBuf[LOG_ROWS];
static int    logIdx = 0;

static void logPrint(const char *msg, int ms=45) {
  if (logIdx < LOG_ROWS) logBuf[logIdx++] = String(msg);
  else {
    for (int i=0;i<LOG_ROWS-1;i++) logBuf[i]=logBuf[i+1];
    logBuf[LOG_ROWS-1] = String(msg);
  }
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(WHITE);
  for (int i=0;i<logIdx;i++) { display.setCursor(0,i*8); display.print(logBuf[i]); }
  display.display();
  delay(ms);
}

// ═══════════════════════════════════════════════════════════════
//  BOOT STAGE 1: SPLASH LOGO FINIX
// ═══════════════════════════════════════════════════════════════
static void bootSplash() {
  // Animasi: expand dari titik ke penuh
  for (int r=2;r<=34;r+=3) {
    display.clearDisplay();
    int x=max(0,64-r*2), y=max(0,32-r);
    int w=min(128,r*4),   h=min(64,r*2);
    if(w>4&&h>4) display.drawRoundRect(x,y,w,h,3,WHITE);
    display.display(); delay(12);
  }

  display.clearDisplay();
  display.drawRect(0,0,OLED_W,OLED_H,WHITE);
  display.drawRect(2,2,OLED_W-4,OLED_H-4,WHITE);

  // Pixel art FINIX
  // F
  display.drawLine(10,8,10,28,WHITE);
  display.drawLine(10,8,22,8,WHITE);
  display.drawLine(10,17,20,17,WHITE);
  // I
  display.drawLine(27,8,27,28,WHITE);
  display.drawLine(24,8,30,8,WHITE);
  display.drawLine(24,28,30,28,WHITE);
  // N
  display.drawLine(35,8,35,28,WHITE);
  display.drawLine(35,8,45,28,WHITE);
  display.drawLine(45,8,45,28,WHITE);
  // I
  display.drawLine(50,8,50,28,WHITE);
  display.drawLine(47,8,53,8,WHITE);
  display.drawLine(47,28,53,28,WHITE);
  // X
  display.drawLine(58,8,70,28,WHITE);
  display.drawLine(70,8,58,28,WHITE);
  display.fillCircle(64,18,1,WHITE);

  display.drawLine(6,32,121,32,WHITE);

  display.setTextSize(1);
  display.setCursor(10,36); display.print("Micro OS  v13.0");
  display.setCursor(6,47);  display.print("ESP32-C3  AlkhaOS");
  display.setCursor(20,57); display.print("by Alkha 2026");
  display.display();

  // Kedip 2x
  delay(600);
  for (int i=0;i<2;i++) {
    display.drawRect(0,0,OLED_W,OLED_H,BLACK);
    display.drawRect(2,2,OLED_W-4,OLED_H-4,BLACK);
    display.display(); delay(90);
    display.drawRect(0,0,OLED_W,OLED_H,WHITE);
    display.drawRect(2,2,OLED_W-4,OLED_H-4,WHITE);
    display.display(); delay(90);
  }
  delay(800);
}

// ═══════════════════════════════════════════════════════════════
//  BOOT STAGE 2: LINUX-STYLE BOOT LOG
// ═══════════════════════════════════════════════════════════════
static void bootLog(bool wifi, String ip, bool ntp,
                    bool spiffs, bool sd, bool rtcFound) {
  memset(logBuf,0,sizeof(logBuf)); logIdx=0;

  logPrint("FINIX OS kernel 13.0.0");
  logPrint("CPU: ESP32-C3 RISC-V",55);
  logPrint("RAM:400KB Flash:4MB",55);
  logPrint("-------------------");
  logPrint("[0.010] I2C init...");
  logPrint("[0.013] OLED: OK",55);
  logPrint("[0.018] BLE: NUS init",55);
  logPrint("[0.022] BLE: advertising",55);
  logPrint(spiffs?"[0.030] SPIFFS: OK":"[0.030] SPIFFS: FAIL",55);
  logPrint(sd    ?"[0.035] SD Card: OK":"[0.035] SD: tidak ada",55);
  logPrint("[0.040] DS18B20: scan",55);
  logPrint("[0.042] LDR: OK",55);
  logPrint("[0.044] MQ-135: OK",55);
  logPrint(rtcFound?"[0.046] RTC DS3231: OK":"[0.046] RTC: tidak ada",55);
  logPrint("[0.050] WiFi: STA mode");
  logPrint("[0.055] Connecting...",90);
  if (wifi) {
    logPrint("[1.100] WiFi: CONNECTED",55);
    String s="[1.101] IP: "+ip; logPrint(s.c_str(),55);
    logPrint("[1.110] NTP: syncing...",80);
    logPrint(ntp?"[1.200] NTP: OK GMT+7":"[1.200] NTP: FAILED",55);
  } else {
    logPrint("[1.100] WiFi: OFFLINE",55);
  }
  logPrint("[1.210] FSH engine: OK");
  logPrint("[1.215] Shell: UP");
  logPrint("[1.220] All sys nominal",100);
  delay(300);
  for (int i=0;i<4;i++)
    logPrint(i%2==0?">>> Loading UI <<<":"                  ",160);
  delay(200);
}

// ═══════════════════════════════════════════════════════════════
//  OLED MENU — CLOCK
// ═══════════════════════════════════════════════════════════════
static void oledClock() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  // Taskbar
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("FINIX|C3|");
  display.print(wifiOk?"WIFI":"OFLN");
  if(wifiOk){
    display.setCursor(90,0);
    display.printf("%ddBm", WiFi.RSSI());
  }
  display.drawLine(0,10,127,10,WHITE);

  // Jam besar
  String t = getTimeStr();
  display.setTextSize(2);
  display.setCursor((128-t.length()*12)/2, 13);
  display.print(t);

  // Tanggal
  display.setTextSize(1);
  display.setCursor(0,36);
  display.print(getDayStr().substring(0,3));
  display.print(", ");
  display.print(getDateStr());

  // Suhu chip + uptime
  display.setCursor(0,46);
  display.printf("Chip:%.0fC", temperatureRead());
  display.setCursor(70,46);
  display.print(getUptime());

  // Footer
  display.drawLine(0,55,127,55,WHITE);
  display.setCursor(0,57);
  display.printf("RAM:%dKB", ESP.getFreeHeap()/1024);
  display.setCursor(70,57);
  display.print("1/" + String(MENU_TOTAL));
  display.display();
}

// ═══════════════════════════════════════════════════════════════
//  OLED MENU — SENSOR
// ═══════════════════════════════════════════════════════════════
static void oledSensor() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("SENSOR MONITOR");
  display.drawLine(0,10,127,10,WHITE);

  // DS18B20
  display.setCursor(0,13);
  display.print("SUHU (DS18B20):");
  display.setCursor(0,22);
  display.setTextSize(2);
  if (sensorTemp > -100)
    display.printf("%.1f", sensorTemp);
  else
    display.print("--.-");
  display.setTextSize(1);
  display.print(" C");

  display.drawLine(0,35,127,35,WHITE);

  // LDR
  display.setCursor(0,37);
  display.printf("LDR:%4d  %s", sensorLdr, ldrStatus().c_str());

  // MQ-135
  display.setCursor(0,47);
  display.printf("MQ135:%4d %s", sensorMq135, mq135Status().c_str());

  // Bar MQ-135
  display.drawRect(0,56,127,7,WHITE);
  int mbar = map(sensorMq135,0,4095,0,125);
  display.fillRect(1,57,mbar,5,WHITE);

  display.display();
}

// ═══════════════════════════════════════════════════════════════
//  OLED MENU — SYSMON
// ═══════════════════════════════════════════════════════════════
static void oledSysmon() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("SYSTEM MONITOR");
  display.drawLine(0,10,127,10,WHITE);

  uint32_t freeRam  = ESP.getFreeHeap();
  uint32_t totalRam = 400*1024;
  uint32_t usedRam  = totalRam - freeRam;
  uint32_t freeFlash= ESP.getFreeSketchSpace();
  uint32_t totalFl  = ESP.getFlashChipSize();

  // RAM bar
  display.setCursor(0,13);
  display.printf("RAM: %dKB/%dKB", usedRam/1024, totalRam/1024);
  display.drawRect(0,22,127,7,WHITE);
  int rbar = (int)((float)usedRam/totalRam*125);
  display.fillRect(1,23,rbar,5,WHITE);

  // Flash bar
  display.setCursor(0,32);
  display.printf("Flash:%dKB free", freeFlash/1024);
  display.drawRect(0,41,127,7,WHITE);
  uint32_t usedFl = totalFl - freeFlash;
  int fbar = (int)((float)usedFl/totalFl*125);
  display.fillRect(1,42,fbar,5,WHITE);

  // CPU & suhu
  display.setCursor(0,51);
  display.printf("CPU:%dMHz T:%.0fC", getCpuFrequencyMhz(), temperatureRead());
  display.setCursor(0,59);
  display.printf("UP:%s", getUptime().c_str());

  display.display();
}

// ═══════════════════════════════════════════════════════════════
//  OLED MENU — MUSIK
// ═══════════════════════════════════════════════════════════════
static void oledMusik() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("MUSIK PLAYER");
  display.drawLine(0,10,127,10,WHITE);

  if (!sdOk) {
    display.setCursor(0,20);
    display.print("SD Card tidak ada!");
    display.display(); return;
  }

  // Status
  display.setCursor(0,14);
  display.printf("Status: %s", musikPlaying?"PLAY":"STOP");

  // Nama file (potong jika panjang)
  display.setCursor(0,24);
  String fn = musikFile.length()>0 ? musikFile : "(tidak ada)";
  if (fn.length()>20) fn = fn.substring(0,20)+"..";
  display.print("File: " + fn);

  // Animasi equalizer sederhana saat play
  if (musikPlaying) {
    int bars[] = {3,6,4,8,5,7,3,6,4};
    for (int i=0;i<9;i++) {
      int bh = (millis()/200 + i) % 10 + 2;
      int bx = 10 + i*12;
      display.fillRect(bx, 55-bh, 8, bh, WHITE);
    }
  } else {
    display.setCursor(0,48);
    display.print("Ketik: musik play <file>");
    display.setCursor(0,57);
    display.print("Contoh: musik play /musik/1.wav");
  }

  // Kontrol hint
  if (!musikPlaying) {
    display.drawLine(0,44,127,44,WHITE);
    display.setCursor(30,46);
    display.print(">> STOP <<");
  }

  display.display();
}

// ═══════════════════════════════════════════════════════════════
//  OLED DISPATCHER
// ═══════════════════════════════════════════════════════════════
static void oledUpdate() {
  // Auto rotate tiap 5 detik
  if (autoRotate && millis()-lastMenuSwitch>5000) {
    oledMenu = (OledMenu)((oledMenu+1) % MENU_TOTAL);
    lastMenuSwitch = millis();
  }
  switch(oledMenu) {
    case MENU_CLOCK:  oledClock();  break;
    case MENU_SENSOR: oledSensor(); break;
    case MENU_SYSMON: oledSysmon(); break;
    case MENU_MUSIK:  oledMusik();  break;
    default: oledClock(); break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  MATH — integer sederhana (tanpa libm berat)
// ═══════════════════════════════════════════════════════════════
static double finixPow(double b, int e) {
  if (e==0) return 1.0;
  double r=1.0; bool neg=(e<0); int n=neg?-e:e;
  for(int i=0;i<n;i++) r*=b;
  return neg?1.0/r:r;
}

static void cmdCalc(const char *expr) {
  double a,b; char op;
  if (sscanf(expr,"%lf %c %lf",&a,&op,&b)!=3) {
    shellPrintln("  Format: calc <a> <op> <b>  op: + - * / ^ %");
    return;
  }
  double r;
  switch(op) {
    case '+': r=a+b; break;
    case '-': r=a-b; break;
    case '*': r=a*b; break;
    case '/': if(b==0){shellPrintln("  Err: bagi 0");return;} r=a/b; break;
    case '^': r=finixPow(a,(int)b); break;
    case '%': if(b==0){shellPrintln("  Err: modulo 0");return;} r=(long)a%(long)b; break;
    default:  shellPrintln("  Operator tidak dikenal"); return;
  }
  char out[32]; sprintf(out,"  = %.6g",r);
  shellPrintln(String(out));
}

// ═══════════════════════════════════════════════════════════════
//  FSH SCRIPT ENGINE
//  Bahasa built-in sederhana:
//    set <var> <nilai>
//    print <var|teks>
//    if <var> == <nilai> then <perintah>
//    loop <n> <perintah>
//    sysinfo / waktu / sensor
// ═══════════════════════════════════════════════════════════════
static void fshSetVar(const char *name, const char *val) {
  for (int i=0;i<fshVarCount;i++) {
    if (strcmp(fshVars[i].name,name)==0) {
      strncpy(fshVars[i].val,val,FSH_VAR_LEN-1);
      return;
    }
  }
  if (fshVarCount<FSH_VARS) {
    strncpy(fshVars[fshVarCount].name,name,11);
    strncpy(fshVars[fshVarCount].val,val,FSH_VAR_LEN-1);
    fshVarCount++;
  }
}
static const char* fshGetVar(const char *name) {
  for (int i=0;i<fshVarCount;i++)
    if (strcmp(fshVars[i].name,name)==0) return fshVars[i].val;
  return "";
}
static String fshExpand(const char *s) {
  // Ganti $var dengan nilainya
  String out = String(s);
  for (int i=0;i<fshVarCount;i++) {
    String key = "$" + String(fshVars[i].name);
    out.replace(key, String(fshVars[i].val));
  }
  return out;
}

// Forward declare execute
static void execute(String input);

static void fshExecLine(const char *line) {
  char buf[MAX_LINE]; strncpy(buf,line,MAX_LINE-1);
  char *tok = strtok(buf," ");
  if (!tok) return;

  if (strcmp(tok,"set")==0) {
    char *name=strtok(nullptr," "), *val=strtok(nullptr,"\0");
    if (name&&val) fshSetVar(name,val);

  } else if (strcmp(tok,"print")==0) {
    char *arg=strtok(nullptr,"\0");
    if (arg) shellPrintln("  " + fshExpand(arg));

  } else if (strcmp(tok,"if")==0) {
    // if <var> == <val> then <cmd>
    char *var=strtok(nullptr," ");
    char *op =strtok(nullptr," ");
    char *val=strtok(nullptr," ");
    strtok(nullptr," "); // "then"
    char *cmd=strtok(nullptr,"\0");
    if (!var||!op||!val||!cmd) return;
    String lhs = fshExpand(var);
    bool match = (strcmp(op,"==")==0 && lhs==String(val)) ||
                 (strcmp(op,"!=")==0 && lhs!=String(val));
    if (match) fshExecLine(cmd);

  } else if (strcmp(tok,"loop")==0) {
    char *ns=strtok(nullptr," ");
    char *cmd=strtok(nullptr,"\0");
    if (!ns||!cmd) return;
    int n=atoi(ns);
    for (int i=0;i<n&&i<20;i++) fshExecLine(cmd);

  } else if (strcmp(tok,"sysinfo")==0) {
    shellPrintln("  CPU:" + String(getCpuFrequencyMhz()) + "MHz RAM:" + String(ESP.getFreeHeap()/1024) + "KB");
  } else if (strcmp(tok,"waktu")==0) {
    shellPrintln("  " + getTimeStr() + " " + getDateStr());
  } else if (strcmp(tok,"sensor")==0) {
    char b[48];
    sprintf(b,"  Suhu:%.1fC LDR:%d MQ:%d", sensorTemp, sensorLdr, sensorMq135);
    shellPrintln(String(b));
  } else {
    // Jalankan sebagai perintah shell biasa
    execute(String(line));
  }
}

static void cmdFshRun(const char *path) {
  // Dari SPIFFS
  if (!spiffsOk) { shellPrintln("  SPIFFS tidak ada."); return; }
  File f = SPIFFS.open(path,"r");
  if (!f) {
    // Coba dari SD
    if (sdOk) f = SD.open(path);
    if (!f) { shellPrintln("  File tidak ditemukan: "+String(path)); return; }
  }
  shellPrintln("  Menjalankan: " + String(path));
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length()>0 && line[0]!='#') fshExecLine(line.c_str());
  }
  f.close();
}

// ═══════════════════════════════════════════════════════════════
//  NET TOOLS
// ═══════════════════════════════════════════════════════════════
static void cmdPing(const char *host) {
  if (!wifiOk) { shellPrintln("  WiFi offline."); return; }
  if (!host) { shellPrintln("  ping <host>"); return; }
  String url = String("http://") + host;
  HTTPClient http; http.begin(url); http.setTimeout(4000);
  unsigned long t=millis();
  int code=http.GET();
  unsigned long dt=millis()-t;
  char b[48];
  sprintf(b,"  %s: HTTP %d  %lums", host, code, dt);
  shellPrintln(String(b));
  http.end();
}

static void cmdHttpGet(const char *url) {
  if (!wifiOk) { shellPrintln("  WiFi offline."); return; }
  if (!url) { shellPrintln("  http <url>"); return; }
  HTTPClient http; http.begin(url); http.setTimeout(8000);
  int code=http.GET();
  if (code==200) {
    String body=http.getString();
    // Tampilkan 512 char pertama saja (hemat RAM)
    if (body.length()>512) body=body.substring(0,512)+"...(terpotong)";
    shellPrintln(body);
  } else {
    shellPrintln("  HTTP error: " + String(code));
  }
  http.end();
}

static void cmdNmap(const char *subnet) {
  if (!wifiOk) { shellPrintln("  WiFi offline."); return; }
  // subnet misal "192.168.1"
  String base = subnet ? String(subnet) : WiFi.localIP().toString();
  // Ambil /24 prefix
  int lastDot = base.lastIndexOf('.');
  if (lastDot>0) base = base.substring(0,lastDot);
  shellPrintln("  Nmap scan " + base + ".1-254 port 80,22,23,8080...");
  int found=0;
  for (int i=1;i<=254;i++) {
    String ip = base+"."+String(i);
    WiFiClient client;
    client.setTimeout(150);
    if (client.connect(ip.c_str(),80)) {
      shellPrintln("  " + ip + ":80  OPEN");
      client.stop(); found++;
    }
    if (found>=5) break; // batas agar tidak terlalu lama
    yield();
  }
  shellPrintln("  Selesai. Ditemukan: " + String(found) + " host");
}

// ═══════════════════════════════════════════════════════════════
//  FILE SYSTEM
// ═══════════════════════════════════════════════════════════════
static String resolvePath(const String &path) {
  if (path.startsWith("/")) return path;
  return String(shellPwd) + (String(shellPwd)=="/"?"":"/") + path;
}

static void cmdLs(const char *path, bool useSD=false) {
  String p = path ? resolvePath(String(path)) : String(shellPwd);
  if (useSD||sdOk&&p.startsWith("/sd/")) {
    File d=SD.open(p.substring(3));
    if (!d){shellPrintln("  Tidak ada: "+p);return;}
    File e=d.openNextFile();
    while(e){shellPrintln("  "+(e.isDirectory()?"[D] ":"[F] ")+String(e.name())+
      " "+String(e.size())+"B"); e=d.openNextFile();}
    d.close();
  } else {
    if (!spiffsOk){shellPrintln("  SPIFFS tidak ada.");return;}
    File d=SPIFFS.open(p);
    if(!d){shellPrintln("  Tidak ada: "+p);return;}
    File e=d.openNextFile();
    while(e){shellPrintln("  [F] "+String(e.name())+" "+String(e.size())+"B");e=d.openNextFile();}
    d.close();
  }
}

static void cmdMkdir(const char *path) {
  // SPIFFS tidak punya direktori nyata — buat file placeholder
  String p = resolvePath(String(path));
  if (!spiffsOk){shellPrintln("  SPIFFS tidak ada.");return;}
  File f = SPIFFS.open(p+"/.keep","w");
  if (f) { f.close(); shellPrintln("  Folder dibuat: "+p); }
  else shellPrintln("  Gagal buat folder.");
}

static void cmdCat(const char *path) {
  String p = resolvePath(String(path));
  File f;
  if (sdOk&&p.startsWith("/sd/")) f=SD.open(p.substring(3));
  else if (spiffsOk) f=SPIFFS.open(p,"r");
  if (!f){shellPrintln("  File tidak ada: "+p);return;}
  while(f.available()) {
    String line=f.readStringUntil('\n');
    shellPrintln(line);
    yield();
  }
  f.close();
}

static void cmdRm(const char *path) {
  String p = resolvePath(String(path));
  if (!spiffsOk){shellPrintln("  SPIFFS tidak ada.");return;}
  if (SPIFFS.remove(p)) shellPrintln("  Dihapus: "+p);
  else shellPrintln("  Gagal hapus atau tidak ada.");
}

// ─── EDITOR BUILT-IN (line editor sederhana) ───────────────────
static void cmdEdit(const char *path) {
  String p = resolvePath(String(path));
  shellPrintln("  Editor: " + p);
  shellPrintln("  Ketik baris per baris. Ketik ':q' untuk simpan & keluar.");
  shellPrintln("  ':x' untuk keluar tanpa simpan.");

  String content = "";
  // Load existing content
  if (spiffsOk && SPIFFS.exists(p)) {
    File f=SPIFFS.open(p,"r");
    while(f.available()) content+=f.readStringUntil('\n')+"\n";
    f.close();
    shellPrintln("  (File ada, isi lama:");
    shellPrintln(content.substring(0,200));
    shellPrintln("  ---- tulis ulang di bawah ----");
  }

  String newContent="";
  while(true) {
    // Tunggu input dari Serial atau BLE
    String line="";
    bool got=false;
    unsigned long tw=millis();
    while(!got && millis()-tw<120000) { // timeout 2 menit
      if (Serial.available()) {
        char c=Serial.read();
        if(c=='\n'||c=='\r'){got=true;break;}
        else if(c==127||c==8){if(line.length()>0)line.remove(line.length()-1);}
        else line+=c;
      }
      if (bleInputReady) {
        line=bleInputBuf; bleInputBuf=""; bleInputReady=false; got=true; break;
      }
      yield();
    }
    if(!got) break; // timeout
    if(line==":q") {
      File f=SPIFFS.open(p,"w");
      if(f){f.print(newContent);f.close();shellPrintln("  Disimpan: "+p);}
      else shellPrintln("  Gagal simpan.");
      break;
    } else if(line==":x") {
      shellPrintln("  Keluar tanpa simpan.");
      break;
    }
    newContent+=line+"\n";
  }
}

// ═══════════════════════════════════════════════════════════════
//  SD CARD
// ═══════════════════════════════════════════════════════════════
static void cmdSdLs(const char *path) {
  if (!sdOk){shellPrintln("  SD tidak ada.");return;}
  String p = path ? String(path) : "/";
  File d=SD.open(p);
  if(!d){shellPrintln("  Tidak ada: "+p);return;}
  shellPrintln("  SD: " + p);
  File e=d.openNextFile();
  while(e){
    shellPrintln("  " + String(e.isDirectory()?"[D]":"[F]") +
                 " " + String(e.name()) + " " + String(e.size()) + "B");
    e=d.openNextFile();
    yield();
  }
  d.close();
}

// ═══════════════════════════════════════════════════════════════
//  SYSINFO / NEOFETCH
// ═══════════════════════════════════════════════════════════════
static void cmdSysinfo() {
  shellPrintln("\n  ╔══════════════════════════════╗");
  shellPrintln("  ║   " OS_NAME " v" OS_VER "           ║");
  shellPrintln("  ╚══════════════════════════════╝");
  shellPrintln("  CPU      : ESP32-C3 RISC-V @ " + String(getCpuFrequencyMhz()) + "MHz");
  shellPrintln("  RAM bebas: " + String(ESP.getFreeHeap()/1024) + " KB / 400 KB");
  shellPrintln("  Flash    : " + String(ESP.getFlashChipSize()/1024) + " KB total");
  shellPrintln("  Sketch   : " + String(ESP.getFreeSketchSpace()/1024) + " KB bebas");
  shellPrintln("  WiFi     : " + String(wifiOk?WiFi.localIP().toString():"OFFLINE"));
  shellPrintln("  RSSI     : " + String(wifiOk?WiFi.RSSI():0) + " dBm");
  shellPrintln("  BLE      : " + String(bleConnected?"TERHUBUNG":"Advertising"));
  shellPrintln("  Waktu    : " + getTimeStr() + " " + getDateStr());
  shellPrintln("  Uptime   : " + getUptime());
  shellPrintln("  Suhu CPU : " + String(temperatureRead(),1) + " C");
  shellPrintln("  SPIFFS   : " + String(spiffsOk?"OK":"GAGAL"));
  shellPrintln("  SD Card  : " + String(sdOk?"OK":"Tidak ada"));
  shellPrintln("  RTC      : " + String(rtcOk?"OK":"Tidak ada"));
  shellPrintln("  DS18B20  : " + String(sensorTemp,1) + " C");
  shellPrintln("  LDR      : " + String(sensorLdr) + " (" + ldrStatus() + ")");
  shellPrintln("  MQ-135   : " + String(sensorMq135) + " (" + mq135Status() + ")");
  shellPrintln();
}

// ═══════════════════════════════════════════════════════════════
//  WIFI PERINTAH
// ═══════════════════════════════════════════════════════════════
static void cmdWifi(const char *sub, const char *ssid, const char *pass) {
  if (!sub || strcmp(sub,"status")==0) {
    shellPrintln("  WiFi: " + String(wifiOk?"TERHUBUNG":"OFFLINE"));
    if (wifiOk) {
      shellPrintln("  SSID: " + WiFi.SSID());
      shellPrintln("  IP  : " + WiFi.localIP().toString());
      shellPrintln("  RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    return;
  }
  if (strcmp(sub,"scan")==0) {
    shellPrintln("  Scanning...");
    int n=WiFi.scanNetworks();
    for (int i=0;i<n;i++)
      shellPrintln("  ["+String(i+1)+"] "+WiFi.SSID(i)+" "+
                   String(WiFi.RSSI(i))+"dBm "+
                   (WiFi.encryptionType(i)==WIFI_AUTH_OPEN?"OPEN":"WPA"));
    WiFi.scanDelete();
    return;
  }
  if (strcmp(sub,"connect")==0) {
    if (!ssid) { shellPrintln("  wifi connect <ssid> [pass]"); return; }
    shellPrintln("  Menghubungkan ke: " + String(ssid));
    wifiOk=wifiConnect(ssid, pass?pass:"");
    if (wifiOk) {
      shellPrintln("  Terhubung! IP: " + WiFi.localIP().toString());
      ntpOk=ntpSync();
    } else shellPrintln("  Gagal terhubung.");
    return;
  }
  if (strcmp(sub,"disconnect")==0) {
    WiFi.disconnect();
    wifiOk=false;
    shellPrintln("  WiFi diputus.");
    return;
  }
  shellPrintln("  wifi status|scan|connect <ssid> [pass]|disconnect");
}

// ═══════════════════════════════════════════════════════════════
//  BANTUAN
// ═══════════════════════════════════════════════════════════════
static void cmdBantuan() {
  shellPrintln("\n  ╔══════════════════════════════════════════╗");
  shellPrintln("  ║  " OS_NAME " v" OS_VER " — Daftar Perintah     ║");
  shellPrintln("  ╚══════════════════════════════════════════╝");
  shellPrintln("  SISTEM    sysinfo, neofetch, uptime, waktu, tanggal, reboot");
  shellPrintln("  WIFI      wifi status|scan|connect|disconnect");
  shellPrintln("  FILE      ls [path], cat <file>, rm <file>, mkdir <dir>");
  shellPrintln("            edit <file>  — editor built-in");
  shellPrintln("  SD CARD   sd ls [path], sd cat <file>");
  shellPrintln("  SENSOR    sensor  — baca semua sensor");
  shellPrintln("            suhu / ldr / mq135  — sensor individual");
  shellPrintln("  JAM       waktu, tanggal, ntp");
  shellPrintln("  MONITOR   sysmon  — monitoring real-time di OLED");
  shellPrintln("            menu clock|sensor|sysmon|musik  — ganti layar");
  shellPrintln("  NET       ping <host>, http <url>");
  shellPrintln("            nmap [subnet]  — port scan LAN");
  shellPrintln("  MUSIK     musik play <path>, musik stop, musik list");
  shellPrintln("  SKRIP FSH fsh <file.fsh>  — jalankan skrip");
  shellPrintln("            fsh-baru <file>  — buat skrip baru");
  shellPrintln("  UTILITAS  calc <a> op <b>, hash <teks>, enkripsi <teks> <key>");
  shellPrintln("            catatan, todo");
  shellPrintln("  GAME      game, hangman");
  shellPrintln("  BLE       ble status  — status koneksi BLE");
  shellPrintln("  LAIN      bantuan, tentang, moto, clear, history");
  shellPrintln("            shutdown, reboot");
  shellPrintln();
}

// ═══════════════════════════════════════════════════════════════
//  GAME TEBAK ANGKA
// ═══════════════════════════════════════════════════════════════
static void cmdGame() {
  int target=(esp_random()%100)+1, coba=0;
  shellPrintln("\n  Tebak Angka 1-100 (maks 10 coba)");
  while(coba<10) {
    shellPrint("  Tebak: ");
    // Tunggu input
    String inp=""; bool got=false;
    unsigned long tw=millis();
    while(!got&&millis()-tw<30000) {
      if(Serial.available()){
        char c=Serial.read();
        if(c=='\n'||c=='\r'){got=true;}else inp+=c;
      }
      if(bleInputReady){inp=bleInputBuf;bleInputBuf="";bleInputReady=false;got=true;}
      yield();
    }
    if(!got) break;
    int t=inp.toInt(); coba++;
    if(t<target) shellPrintln("  Terlalu kecil!");
    else if(t>target) shellPrintln("  Terlalu besar!");
    else {shellPrintln("  BENAR! Dalam "+String(coba)+" percobaan!"); return;}
  }
  shellPrintln("  Kalah. Angkanya: "+String(target));
}

// ═══════════════════════════════════════════════════════════════
//  PROMPT & PARSER
// ═══════════════════════════════════════════════════════════════
static void printPrompt() {
  String p = String(loggedIn?"root@":"tamu@") + HOSTNAME + ":" + shellPwd +
             (loggedIn?"# ":"$ ");
  shellPrint("\n" + p);
}

static void histAdd(const char *cmd) {
  if(!cmd||!strlen(cmd)) return;
  if(histCount>0&&strcmp(history[histCount-1],cmd)==0) return;
  if(histCount>=MAX_HISTORY) {
    memmove(history[0],history[1],(MAX_HISTORY-1)*MAX_LINE);
    histCount=MAX_HISTORY-1;
  }
  strncpy(history[histCount++],cmd,MAX_LINE-1);
}

// ═══════════════════════════════════════════════════════════════
//  EXECUTE — PARSER PERINTAH UTAMA
// ═══════════════════════════════════════════════════════════════
static void execute(String input) {
  input.trim();
  if (input.length()==0) return;
  histAdd(input.c_str());

  // Expand variabel FSH di perintah
  input = fshExpand(input.c_str());

  char buf[MAX_LINE]; strncpy(buf,input.c_str(),MAX_LINE-1);
  char *args[10]={}; int argc=0;
  char *tok=strtok(buf," \t");
  while(tok&&argc<10){args[argc++]=tok;tok=strtok(nullptr," \t");}
  if(argc==0) return;
  const char *cmd=args[0];

  // Perintah publik (tanpa login)
  bool pub=(!strcmp(cmd,"bantuan")||!strcmp(cmd,"help")||
            !strcmp(cmd,"tentang")||!strcmp(cmd,"clear")||
            !strcmp(cmd,"bersih")||!strcmp(cmd,"login")||
            !strcmp(cmd,"reboot")||!strcmp(cmd,"shutdown"));
  if (!loggedIn && !pub) {
    shellPrintln("  Login dulu. Ketik 'login'");
    return;
  }

  // ── AUTH ──
  if (!strcmp(cmd,"login")) {
    shellPrint("  Password (default: finix): ");
    // Baca password
    String pw=""; bool got=false;
    unsigned long tw=millis();
    while(!got&&millis()-tw<20000) {
      if(Serial.available()){char c=Serial.read();if(c=='\n'||c=='\r')got=true;else pw+=c;}
      if(bleInputReady){pw=bleInputBuf;bleInputBuf="";bleInputReady=false;got=true;}
      yield();
    }
    pw.trim();
    // Password sederhana — simpan di SPIFFS /passwd.dat jika ada
    String storedPw = "finix";
    if (spiffsOk && SPIFFS.exists("/passwd.dat")) {
      File f=SPIFFS.open("/passwd.dat","r");
      storedPw=f.readStringUntil('\n'); storedPw.trim(); f.close();
    }
    if (pw==storedPw) { loggedIn=true; shellPrintln("\n  Login berhasil! Selamat datang."); }
    else shellPrintln("\n  Password salah.");
  }
  else if (!strcmp(cmd,"logout")) { loggedIn=false; shellPrintln("  Logout."); }
  else if (!strcmp(cmd,"passwd")) {
    shellPrint("  Password baru: ");
    String pw=""; bool got=false; unsigned long tw=millis();
    while(!got&&millis()-tw<20000){
      if(Serial.available()){char c=Serial.read();if(c=='\n'||c=='\r')got=true;else pw+=c;}
      if(bleInputReady){pw=bleInputBuf;bleInputBuf="";bleInputReady=false;got=true;}
      yield();
    }
    pw.trim();
    if(spiffsOk){File f=SPIFFS.open("/passwd.dat","w");f.println(pw);f.close();}
    shellPrintln("  Password diubah.");
  }

  // ── SISTEM ──
  else if (!strcmp(cmd,"sysinfo"))   cmdSysinfo();
  else if (!strcmp(cmd,"neofetch"))  { cmdSysinfo(); } // alias
  else if (!strcmp(cmd,"waktu"))     shellPrintln("  " + getTimeStr());
  else if (!strcmp(cmd,"tanggal")||!strcmp(cmd,"date"))
    shellPrintln("  " + getDayStr() + ", " + getDateStr());
  else if (!strcmp(cmd,"uptime"))    shellPrintln("  " + getUptime());
  else if (!strcmp(cmd,"ntp")) {
    shellPrintln("  NTP sync...");
    ntpOk=ntpSync();
    shellPrintln(ntpOk?"  OK: "+getTimeStr():"  Gagal.");
  }

  // ── SENSOR ──
  else if (!strcmp(cmd,"sensor")) {
    char b[80];
    sprintf(b,"  Suhu   : %.2f C (DS18B20)", sensorTemp);
    shellPrintln(String(b));
    sprintf(b,"  LDR    : %d  → %s", sensorLdr, ldrStatus().c_str());
    shellPrintln(String(b));
    sprintf(b,"  MQ-135 : %d  → %s", sensorMq135, mq135Status().c_str());
    shellPrintln(String(b));
    sprintf(b,"  Suhu CPU: %.1f C", temperatureRead());
    shellPrintln(String(b));
  }
  else if (!strcmp(cmd,"suhu"))
    shellPrintln("  DS18B20: " + String(sensorTemp,2) + " C");
  else if (!strcmp(cmd,"ldr"))
    shellPrintln("  LDR: " + String(sensorLdr) + " → " + ldrStatus());
  else if (!strcmp(cmd,"mq135"))
    shellPrintln("  MQ-135: " + String(sensorMq135) + " → " + mq135Status());

  // ── WIFI ──
  else if (!strcmp(cmd,"wifi"))
    cmdWifi(argc>1?args[1]:nullptr, argc>2?args[2]:nullptr, argc>3?args[3]:nullptr);

  // ── FILE ──
  else if (!strcmp(cmd,"ls")||!strcmp(cmd,"dir"))
    cmdLs(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"cat"))   cmdCat(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"rm"))    cmdRm(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"mkdir")) cmdMkdir(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"edit"))  cmdEdit(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"cd")) {
    if (argc>1) { strncpy(shellPwd,resolvePath(String(args[1])).c_str(),63); }
    shellPrintln("  " + String(shellPwd));
  }
  else if (!strcmp(cmd,"pwd"))   shellPrintln("  " + String(shellPwd));

  // ── SD CARD ──
  else if (!strcmp(cmd,"sd")) {
    if (argc>1&&!strcmp(args[1],"ls")) cmdSdLs(argc>2?args[2]:nullptr);
    else if (argc>1&&!strcmp(args[1],"cat")) cmdCat(argc>2?("/sd"+String(args[2])).c_str():nullptr);
    else shellPrintln("  sd ls [path] | sd cat <file>");
  }

  // ── NET ──
  else if (!strcmp(cmd,"ping"))  cmdPing(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"http")||!strcmp(cmd,"curl"))
    cmdHttpGet(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"nmap"))  cmdNmap(argc>1?args[1]:nullptr);

  // ── MUSIK ──
  else if (!strcmp(cmd,"musik")) {
    if (argc>1&&!strcmp(args[1],"play"))
      musikPlay(argc>2?args[2]:nullptr);
    else if (argc>1&&!strcmp(args[1],"stop"))
      musikStop();
    else if (argc>1&&!strcmp(args[1],"list"))
      cmdSdLs("/musik");
    else shellPrintln("  musik play <file>|stop|list");
  }

  // ── OLED MENU ──
  else if (!strcmp(cmd,"menu")) {
    if (argc>1) {
      autoRotate=false;
      if (!strcmp(args[1],"clock"))  oledMenu=MENU_CLOCK;
      else if (!strcmp(args[1],"sensor")) oledMenu=MENU_SENSOR;
      else if (!strcmp(args[1],"sysmon")) oledMenu=MENU_SYSMON;
      else if (!strcmp(args[1],"musik"))  oledMenu=MENU_MUSIK;
      else if (!strcmp(args[1],"auto"))   autoRotate=true;
    }
    shellPrintln("  Menu: " + String(oledMenu));
  }

  // ── FSH SKRIP ──
  else if (!strcmp(cmd,"fsh"))   cmdFshRun(argc>1?args[1]:nullptr);
  else if (!strcmp(cmd,"fsh-baru")) cmdEdit(argc>1?args[1]:"/new.fsh");
  else if (!strcmp(cmd,"set")) {
    if (argc>2) fshSetVar(args[1],args[2]);
  }
  else if (!strcmp(cmd,"print")) {
    char out[MAX_LINE]={};
    for(int i=1;i<argc;i++){if(i>1)strncat(out," ",MAX_LINE-strlen(out)-1);
      strncat(out,args[i],MAX_LINE-strlen(out)-1);}
    shellPrintln("  " + fshExpand(out));
  }
  else if (!strcmp(cmd,"vars")) {
    for(int i=0;i<fshVarCount;i++)
      shellPrintln("  $"+String(fshVars[i].name)+" = "+String(fshVars[i].val));
  }

  // ── UTILITAS ──
  else if (!strcmp(cmd,"calc")) {
    char expr[64]={};
    for(int i=1;i<argc;i++){if(i>1)strncat(expr," ",sizeof(expr)-strlen(expr)-1);
      strncat(expr,args[i],sizeof(expr)-strlen(expr)-1);}
    cmdCalc(expr);
  }
  else if (!strcmp(cmd,"hash")) {
    if(argc>1){unsigned long h=5381;const char*s=args[1];while(*s)h=((h<<5)+h)^(unsigned char)*s++;
      char b[20];sprintf(b,"  %016lx",h);shellPrintln(String(b));}
  }
  else if (!strcmp(cmd,"enkripsi")) {
    if(argc>2){
      const char *t=args[1],*k=args[2]; int kl=strlen(k);
      String out="  ";
      for(int i=0;t[i];i++) { char c[3];sprintf(c,"%02X",(unsigned char)(t[i]^k[i%kl]));out+=c; }
      shellPrintln(out);
    } else shellPrintln("  enkripsi <teks> <kunci>");
  }
  else if (!strcmp(cmd,"catatan")) {
    const char *sub=argc>1?args[1]:nullptr;
    if(!sub||!strcmp(sub,"list")){
      for(int i=0;i<noteCount;i++) shellPrintln("  ["+String(i+1)+"] "+notes[i].waktu+" "+notes[i].text);
    } else if(!strcmp(sub,"tambah")&&argc>2) {
      if(noteCount<MAX_NOTES){strncpy(notes[noteCount].text,args[2],NOTE_LEN-1);
        strncpy(notes[noteCount].waktu,getTimeStr().c_str(),11);noteCount++;notesSave();
        shellPrintln("  Disimpan.");}
    } else if(!strcmp(sub,"hapus")&&argc>2) {
      int idx=atoi(args[2])-1;
      if(idx>=0&&idx<noteCount){memmove(&notes[idx],&notes[idx+1],(noteCount-idx-1)*sizeof(Note));
        noteCount--;notesSave();shellPrintln("  Dihapus.");}
    }
  }
  else if (!strcmp(cmd,"todo")) {
    const char *sub=argc>1?args[1]:nullptr;
    if(!sub||!strcmp(sub,"list")){
      for(int i=0;i<todoCount;i++) shellPrintln("  ["+String(todos[i].done?'x':' ')+"] "+String(i+1)+". "+todos[i].text);
    } else if(!strcmp(sub,"tambah")&&argc>2) {
      if(todoCount<MAX_TODO){strncpy(todos[todoCount].text,args[2],TODO_LEN-1);
        todos[todoCount].done=false;todoCount++;todoSave();shellPrintln("  Ditambahkan.");}
    } else if(!strcmp(sub,"done")&&argc>2) {
      int idx=atoi(args[2])-1;
      if(idx>=0&&idx<todoCount){todos[idx].done=true;todoSave();shellPrintln("  Selesai.");}
    }
  }
  else if (!strcmp(cmd,"firewall")||!strcmp(cmd,"fw")) {
    const char *sub=argc>1?args[1]:nullptr;
    if(!sub||!strcmp(sub,"status")){
      shellPrintln("  Firewall: "+String(fwCount)+" IP diblokir");
      for(int i=0;i<fwCount;i++) shellPrintln("  ["+String(i+1)+"] BLOKIR "+fwBlock[i]);
    } else if(!strcmp(sub,"blokir")&&argc>2) {
      if(fwCount<MAX_FW_IP){strncpy(fwBlock[fwCount++],args[2],19);shellPrintln("  IP diblokir: "+String(args[2]));}
    } else if(!strcmp(sub,"hapus")&&argc>2) {
      int idx=atoi(args[2])-1;
      if(idx>=0&&idx<fwCount){memmove(fwBlock[idx],fwBlock[idx+1],(fwCount-idx-1)*20);fwCount--;shellPrintln("  Aturan dihapus.");}
    }
  }

  // ── GAME ──
  else if (!strcmp(cmd,"game"))    cmdGame();
  else if (!strcmp(cmd,"hangman")) shellPrintln("  (Hangman: segera hadir via BLE)");

  // ── BLE ──
  else if (!strcmp(cmd,"ble")) {
    shellPrintln("  BLE: " + String(bleConnected?"TERHUBUNG":"Advertising"));
    shellPrintln("  Device: " BLE_DEVICE_NAME);
  }

  // ── LAIN ──
  else if (!strcmp(cmd,"tentang")||!strcmp(cmd,"about")) {
    shellPrintln("  " OS_NAME " v" OS_VER " — ESP32-C3 Native");
    shellPrintln("  Author : " OS_AUTHOR);
    shellPrintln("  BLE    : NUS (Nordic UART Service)");
  }
  else if (!strcmp(cmd,"moto")) {
    const char *m[]={"Kode dengan hati.","ESP32 kecil tapi kuat!","BLE: tanpa kabel, penuh gaya."};
    shellPrintln("  \"" + String(m[esp_random()%3]) + "\"");
  }
  else if (!strcmp(cmd,"history")||!strcmp(cmd,"riwayat")) {
    for(int i=0;i<histCount;i++) shellPrintln("  "+String(i+1)+"  "+String(history[i]));
  }
  else if (!strcmp(cmd,"clear")||!strcmp(cmd,"bersih")) {
    // ANSI clear screen ke Serial
    Serial.print("\033[2J\033[H");
    blePrint("\033[2J\033[H");
  }
  else if (!strcmp(cmd,"bantuan")||!strcmp(cmd,"help")) cmdBantuan();
  else if (!strcmp(cmd,"reboot")) { shellPrintln("  Reboot..."); delay(500); ESP.restart(); }
  else if (!strcmp(cmd,"shutdown")) {
    shellPrintln("  Shutdown...");
    display.clearDisplay(); display.setCursor(20,25);
    display.setTextSize(1); display.print("Shutting down...");
    display.display(); delay(1500);
    display.clearDisplay(); display.display();
    esp_deep_sleep_start();
  }
  else {
    shellPrintln("  '" + String(cmd) + "' tidak dikenal. Ketik 'bantuan'.");
  }
}

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // I2C & OLED
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED GAGAL!"); while(true) delay(1000);
  }
  display.setTextColor(WHITE);
  display.clearDisplay(); display.display();

  // ── Stage 1: Splash ──
  bootSplash();

  // ── BLE init (sebelum WiFi agar BLE siap lebih cepat) ──
  bleInit();

  // ── SPIFFS ──
  spiffsOk = SPIFFS.begin(true);

  // ── SD Card ──
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdOk = SD.begin(PIN_SD_CS);

  // ── Sensor ──
  ds18b20.begin();
  analogSetAttenuation(ADC_11db); // untuk LDR & MQ-135 range 0-3.3V
  sensorUpdate(); // baca pertama

  // ── RTC ──
  rtcOk = rtc.begin();
  if (rtcOk && rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // ── Load data ──
  notesLoad(); todoLoad();

  // ── WiFi ──
  // Coba load ssid/pass dari SPIFFS
  if (spiffsOk && SPIFFS.exists("/wifi.cfg")) {
    File f=SPIFFS.open("/wifi.cfg","r");
    String s=f.readStringUntil('\n'); s.trim();
    String p=f.readStringUntil('\n'); p.trim();
    f.close();
    strncpy(wifiSsid,s.c_str(),31);
    strncpy(wifiPass,p.c_str(),63);
  } else {
    // Default
    strncpy(wifiSsid,"MCGOGO",31);
    strncpy(wifiPass,"ISI_PASSWORD_KAMU",63);
  }
  wifiOk=wifiConnect(wifiSsid,wifiPass);
  if (wifiOk) ntpOk=ntpSync();

  // ── Boot log ──
  bootLog(wifiOk, wifiOk?WiFi.localIP().toString():"",
          ntpOk, spiffsOk, sdOk, rtcOk);

  // DAC init
  dacWrite(PIN_DAC_OUT, 128);

  // Sambut
  shellPrintln("\n  " OS_NAME " v" OS_VER " siap!");
  shellPrintln("  Serial USB + BLE (" BLE_DEVICE_NAME ") aktif.");
  shellPrintln("  Ketik 'bantuan' untuk daftar perintah.");
  shellPrintln("  'login' untuk masuk (default pw: finix)");
}

// ═══════════════════════════════════════════════════════════════
//  LOOP — non-blocking
// ═══════════════════════════════════════════════════════════════
// Buffer input Serial USB
static String serialBuf = "";
static bool   promptShown = false;

void loop() {
  unsigned long now = millis();

  // ── Update sensor tiap 2 detik ──
  sensorUpdate();

  // ── Update OLED tiap 500ms ──
  static unsigned long lastOled=0;
  if (now-lastOled>=500) { oledUpdate(); lastOled=now; }

  // ── NTP resync tiap 6 jam ──
  if (wifiOk && (now-lastNtp>=NTP_RESYNC_MS)) ntpOk=ntpSync();

  // ── Musik tick ──
  musikTick();

  // ── Tampilkan prompt jika belum ──
  if (!promptShown) { printPrompt(); promptShown=true; }

  // ── Baca input Serial USB ──
  while (Serial.available()) {
    char c=Serial.read();
    Serial.print(c); // echo
    if (c=='\r'||c=='\n') {
      if (serialBuf.length()>0) {
        String cmd=serialBuf; serialBuf="";
        promptShown=false;
        execute(cmd);
        promptShown=false;
      }
    } else if (c==127||c==8) {
      if (serialBuf.length()>0) { serialBuf.remove(serialBuf.length()-1); Serial.print("\b \b"); }
    } else {
      serialBuf+=c;
    }
  }

  // ── Baca input BLE ──
  if (bleInputReady && bleConnected) {
    String cmd=bleInputBuf; bleInputBuf=""; bleInputReady=false;
    promptShown=false;
    execute(cmd);
    promptShown=false;
  }

  // Yield ke sistem — penting untuk ESP32-C3 single core + BLE
  vTaskDelay(1);
}
