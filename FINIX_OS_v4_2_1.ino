/*
 * ======================================================================
 *              F I N I X  O S  v 4 . 2
 *      Mikrokomputer ESP32 - Stable & Lean (Menu Angka)
 * ======================================================================
 *  Hardware:
 *    - ESP32 Classic 30/38-pin
 *    - OLED 0.96" SSD1306 128x64 I2C
 *    - RTC DS3231  SDA=21 SCL=22
 *    - Keypad Membran 4x4
 * ----------------------------------------------------------------------
 *  Wiring Keypad:
 *    ROW -> GPIO 13,12,14,27   COL -> GPIO 26,25,33,32
 * ----------------------------------------------------------------------
 *  Fungsi Tombol di Terminal:
 *    0-9 = ketik nomor menu        B = Backspace
 *    C   = Clear input             D = ENTER (jalankan nomor)
 *    *   = Scroll naik (saat input kosong)
 *    #   = Scroll turun (saat input kosong)
 *    Tahan D 1.5 detik              = Masuk Sleep / Bangun
 * ----------------------------------------------------------------------
 *  Library: Adafruit SSD1306, Adafruit GFX, RTClib, Keypad
 * ----------------------------------------------------------------------
 *  Setup: Ganti WIFI_SSID & WIFI_PASS lalu upload
 *  PIN default: 1234
 * ----------------------------------------------------------------------
 *  Perbaikan v4.1 -> v4.2:
 *  FIX1 - Perintah teks ("help","calc","snake",dst) DIGANTI nomor menu
 *         (1=help, 2=about, ... 13=halt). Tidak perlu lagi T9 multi-tap
 *         untuk mengetik huruf -> input cukup angka, jauh lebih cepat
 *         diketik di keypad 4x4.
 *  FIX2 - Bug kompilasi: macro AUTHOR dipakai di about() tapi tidak
 *         pernah didefinisikan -> sekarang didefinisikan (OS_AUTHOR).
 *  FIX3 - Mode T9/alphaMode (tidak lagi relevan tanpa perintah huruf)
 *         dihapus total -> kode lebih ramping, RAM lebih hemat, dan
 *         UI tidak lagi menampilkan indikator [1]/[A] yang membingungkan.
 *  FIX4 - Header & help menu dirombak agar nomor & nama fungsi sejajar
 *         dan mudah dibaca pada layar 128x64 (kolom rapi, tidak terpotong).
 *  FIX5 - Validasi input nomor menu: nomor di luar rentang (bukan 1-13)
 *         akan menampilkan pesan jelas, bukan diam saja.
 *  FIX6 - Sleep & wake (tahan D 1.5 detik) dipertahankan persis seperti
 *         v4.1, ditambah opsi sleep lewat menu nomor 11.
 *  FIX7 - Reboot & halt sekarang juga tersedia sebagai nomor menu
 *         (12=reboot, 13=halt), tidak hanya lewat command line lama.
 *  FIX8 - Lompatan game Dino (jump) terlalu tinggi/jauh -> kecepatan
 *         lompat awal dikurangi (-7.8 -> -4.6) dan gravitasi dinaikkan
 *         (0.38 -> 0.55) agar lompatan pas untuk melewati obstacle.
 *  FIX9 - Menu baru 14=clock: mode jam digital penuh layar (jam:menit:
 *         detik besar di tengah, tanggal di bawahnya), keluar dengan C.
 * ======================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <Keypad.h>
#include <WiFi.h>
#include <esp_sleep.h>

// ======================================================================
//  KONFIGURASI - GANTI SEBELUM UPLOAD
// ======================================================================
#define WIFI_SSID   "NAMA_WIFI"
#define WIFI_PASS   "PASSWORD_WIFI"
#define NTP_SRV     "pool.ntp.org"
#define TZ_OFFSET   25200L      // WIB=25200 WITA=28800 WIT=32400

// ======================================================================
//  HARDWARE
// ======================================================================
#define OLED_W    128
#define OLED_H     64
#define OLED_ADDR 0x3C
#define SDA_PIN    21
#define SCL_PIN    22

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
RTC_DS3231 rtc;

// -- Keypad 4x4 ----------------------------------------------------------
const byte KR = 4, KC = 4;
char kmap[KR][KC] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};
byte rowPins[KR] = {13,12,14,27};
byte colPins[KC] = {26,25,33,32};
Keypad kpad = Keypad(makeKeymap(kmap), rowPins, colPins, KR, KC);

// ======================================================================
//  IDENTITAS OS
// ======================================================================
#define OS_VER    "v4.2"
#define OS_HOST   "FINIX"
#define OS_USER   "alkha"
#define OS_AUTHOR "Alkha / Nurudeen"   // FIX2: sebelumnya tidak terdefinisi

// PIN login - hanya angka, max 8 digit
static char osPin[9] = "1234";

// ======================================================================
//  STATE MACHINE
// ======================================================================
enum State {
    ST_BOOT,
    ST_LOGIN,
    ST_TERM,
    ST_CALC,
    ST_SNAKE,
    ST_JUMP,
    ST_PONG,
    ST_CLOCK,
    ST_SLEEP
};
static State curSt = ST_BOOT;

// ======================================================================
//  TERMINAL BUFFER
//  Statis: 30 baris x 22 karakter = 660 byte, tidak pernah fragmentasi
// ======================================================================
#define TB_ROWS  30
#define TB_COLS  22
static char    tbuf[TB_ROWS][TB_COLS]; // ring buffer baris output
static uint8_t tbHead  = 0;            // slot berikutnya yang ditulis
static uint8_t tbCount = 0;            // jumlah baris terisi
static int8_t  tbScroll = 0;           // 0 = lihat baris terbaru

// -- Input (FIX1: sekarang murni numerik, T9 tidak diperlukan) ----------
#define CMD_MAX  3        // nomor menu maksimal 3 digit (cukup s/d 999)
static char    cmdBuf[CMD_MAX+1] = {0};
static uint8_t cmdLen = 0;

// ======================================================================
//  VARIABEL GLOBAL LAIN
// ======================================================================
static uint32_t bootMs     = 0;
static uint8_t  loginTries = 0;
static char     loginBuf[9] = {0};
static uint8_t  loginLen   = 0;

// -- Sleep / tahan tombol D ----------------------------------------------
// Mekanisme tahan D:
//   1. getKey() mendeteksi D pertama kali ditekan -> catat waktu
//   2. Loop berikutnya cek kpad.isPressed('D') tanpa mengonsumsi event
//   3. Jika tahan >= HOLD_MS -> trigger aksi (sleep atau wake)
//   4. Jika D dilepas sebelum HOLD_MS -> anggap tap biasa (ENTER)
static uint32_t holdStartMs  = 0;     // waktu mulai tahan D
static bool     holdTracking = false; // sedang menghitung tahan D?
static bool     holdFired    = false; // aksi sudah ditrigger? (anti-repeat)
static bool     holdWasTap   = false; // D dilepas cepat = tap = ENTER
#define HOLD_MS 1500

// -- Hari & bulan ----------------------------------------------------------
static const char* WDAY[7] = {"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
static const char* MON[12] = {"Jan","Feb","Mar","Apr","Mei","Jun",
                               "Jul","Agt","Sep","Okt","Nov","Des"};

// ======================================================================
//  UTILITAS
// ======================================================================

// Cetak RAM dalam KB (misal "520KB"), bukan byte mentah
static void fmtKB(char* out, uint8_t sz, uint32_t bytes) {
    uint32_t kb = (bytes + 512) / 1024;
    if (kb == 0) snprintf(out, sz, "%uB",  (unsigned)bytes);
    else         snprintf(out, sz, "%uKB", (unsigned)kb);
}

// Cetak satu baris ke terminal ring buffer
static void tPrint(const char* s) {
    if (!s) return;
    uint8_t len = (uint8_t)strlen(s);
    // Tulis minimal 1 baris (untuk string kosong = baris kosong)
    uint8_t chunks = (len == 0) ? 1 : (len + TB_COLS - 2) / (TB_COLS - 1);
    for (uint8_t i = 0; i < chunks; i++) {
        uint8_t off = i * (TB_COLS - 1);
        uint8_t n   = (len > off) ? (len - off) : 0;
        if (n > TB_COLS - 1) n = TB_COLS - 1;
        if (n > 0) memcpy(tbuf[tbHead], s + off, n);
        tbuf[tbHead][n] = '\0';
        tbHead = (tbHead + 1) % TB_ROWS;
        if (tbCount < TB_ROWS) tbCount++;
    }
    tbScroll = 0;   // auto-scroll ke bawah saat ada output baru
}

static void tClear() {
    memset(tbuf, 0, sizeof(tbuf));
    tbHead = 0; tbCount = 0; tbScroll = 0;
}

// Ambil baris ke-n dari bawah: 0 = paling baru, 1 = satu sebelumnya, dst.
static const char* tGetLine(uint8_t fromBottom) {
    if (fromBottom >= tbCount) return "";
    int16_t idx = (int16_t)tbHead - 1 - (int16_t)fromBottom;
    while (idx < 0) idx += TB_ROWS;
    return tbuf[(uint8_t)(idx % TB_ROWS)];
}

// --- Tampilan header bar -------------------------------------------------
static void drawHeader(const char* title) {
    oled.fillRect(0, 0, OLED_W, 9, WHITE);
    oled.setTextColor(BLACK); oled.setTextSize(1);
    oled.setCursor(2, 1); oled.print(title);
    oled.setTextColor(WHITE);
}

// ======================================================================
//  RENDER TERMINAL
//  Layout 128x64 (font 6x8):
//    y=0..7  : prompt (FINIX> nomor)            <- ATAS
//    y=8     : garis pemisah
//    y=9..63 : 7 baris output                   <- bawah garis
// ======================================================================
static void renderTerm() {
    oled.clearDisplay();
    oled.setTextSize(1); oled.setTextColor(WHITE);

    // -- Prompt (baris paling atas) --------------------------------------
    // Format: "FINIX> 7"   (cukup nomor menu, jauh lebih ringkas dari
    // path "user@host~$" yang dulu dipakai untuk perintah teks)
    char promptLine[CMD_MAX + 10];
    snprintf(promptLine, sizeof(promptLine), "%s> %s", OS_HOST, cmdBuf);
    oled.setCursor(0, 0);
    oled.print(promptLine);

    // Kursor blink di akhir prompt
    uint8_t curX = (uint8_t)(strlen(promptLine) * 6);
    if ((millis() / 400) % 2 == 0 && curX < OLED_W - 3)
        oled.fillRect(curX, 0, 3, 7, WHITE);

    // Indikator scroll di kanan atas (tidak tumpuk dengan cursor)
    if (tbScroll > 0) {
        char sb[8]; snprintf(sb, sizeof(sb), "^%-2d", tbScroll);
        oled.setCursor(104, 0);
        oled.print(sb);
    }

    // -- Garis pemisah ----------------------------------------------------
    oled.drawFastHLine(0, 8, OLED_W, WHITE);

    // -- Output (7 baris, y=9..63) -----------------------------------------
    // row 0 = y=9  -> baris paling atas area output = baris PALING LAMA
    // row 6 = y=57 -> baris paling bawah = baris PALING BARU
    for (uint8_t row = 0; row < 7; row++) {
        uint8_t fromBot = (6 - row) + (uint8_t)tbScroll;
        oled.setCursor(0, 9 + row * 8);
        oled.print(tGetLine(fromBot));
    }

    oled.display();
}

// ======================================================================
//  LOGO LINUX MINT (daun dalam lingkaran)
// ======================================================================
static void drawMintLogo(int16_t cx, int16_t cy, uint8_t r) {
    oled.drawCircle(cx, cy, r,     WHITE);
    oled.drawCircle(cx, cy, r - 1, WHITE);
    oled.fillCircle(cx, cy, 2, WHITE);
    int16_t top_y  = cy - r*60/100;
    int16_t mid_lx = cx - r*38/100, mid_ly = cy + r*5/100;
    int16_t mid_rx = cx + r*38/100, mid_ry = mid_ly;
    int16_t bot_y  = cy + r*50/100;
    oled.drawLine(mid_lx, mid_ly, cx,     top_y,  WHITE);
    oled.drawLine(mid_rx, mid_ry, cx,     top_y,  WHITE);
    oled.drawLine(mid_lx, mid_ly, cx,     bot_y,  WHITE);
    oled.drawLine(mid_rx, mid_ry, cx,     bot_y,  WHITE);
    oled.drawLine(cx, top_y, cx, bot_y, WHITE);
    oled.drawLine(cx, cy, cx - r*20/100, cy - r*20/100, WHITE);
    oled.drawLine(cx, cy, cx + r*20/100, cy - r*20/100, WHITE);
}

// ======================================================================
//  BOOT SEQUENCE
// ======================================================================
static const char* BLOG[] = {
    "[0.00] FINIX kernel init",
    "[0.08] I2C SDA:21 SCL:22 OK",
    "[0.20] SSD1306 64px OK",
    "[0.31] DS3231 RTC OK",
    "[0.42] Keypad 4x4 OK",
    "[0.55] Checking RTC...",
    "[0.67] NTP sync...",
    "[0.80] Shell ready.",
};
#define BLOG_N 8

static void showBoot() {
    // Fase 1: Logo + nama
    oled.clearDisplay();
    drawMintLogo(36, 26, 22);
    oled.setTextSize(1); oled.setTextColor(WHITE);
    oled.setCursor(66, 16); oled.print("FINIX OS");
    oled.setCursor(66, 28); oled.print(OS_VER);
    oled.setCursor(66, 42); oled.print("Linux Mint");
    oled.setCursor(66, 52); oled.print("ESP32");
    oled.display();
    delay(1500);

    // Fase 2: Boot log bergulir
    for (uint8_t i = 0; i < BLOG_N; i++) {
        oled.clearDisplay();
        oled.setTextSize(1); oled.setTextColor(WHITE);
        uint8_t start = (i >= 6) ? (uint8_t)(i - 5) : 0;
        for (uint8_t j = start; j <= i; j++) {
            oled.setCursor(0, (j - start) * 9);
            oled.print(BLOG[j]);
        }
        oled.drawRect(0, 57, OLED_W, 6, WHITE);
        uint8_t w = (uint8_t)((uint16_t)(i + 1) * (OLED_W - 4) / BLOG_N);
        if (w > 0) oled.fillRect(2, 59, w, 2, WHITE);
        oled.display();
        delay(220);
    }
    delay(250);
}

// ======================================================================
//  NTP SYNC
// ======================================================================
static bool rtcIsValid() {
    DateTime n = rtc.now();
    return !(n.year() == 2000 && n.month() == 1 && n.day() == 1);
}

static void oledMsg(const char* l1, const char* l2 = nullptr) {
    oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
    oled.setCursor(0, 14); oled.print(l1);
    if (l2) { oled.setCursor(0, 30); oled.print(l2); }
    oled.display();
}

static bool doNTP() {
    oledMsg("Hubungkan WiFi...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t t0  = millis();
    uint8_t  dot = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
        oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
        oled.setCursor(0, 10); oled.print("WiFi connecting...");
        oled.setCursor(0, 24); oled.print(WIFI_SSID);
        oled.setCursor(0, 38);
        for (uint8_t d = 0; d < dot % 5; d++) oled.print(".");
        dot++; oled.display(); delay(350);
    }

    if (WiFi.status() != WL_CONNECTED) {
        oledMsg("WiFi GAGAL!", "Lanjut tanpa NTP");
        WiFi.mode(WIFI_OFF); delay(1200); return false;
    }

    oledMsg("WiFi OK!", "Ambil waktu NTP...");
    configTime(TZ_OFFSET, 0, NTP_SRV);
    struct tm ti; t0 = millis(); bool ok = false;
    while (millis() - t0 < 9000) {
        if (getLocalTime(&ti)) { ok = true; break; }
        delay(200);
    }
    if (ok) {
        rtc.adjust(DateTime(
            ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
            ti.tm_hour, ti.tm_min, ti.tm_sec));
        char b[22];
        snprintf(b, sizeof(b), "%02d:%02d %02d/%02d/%04d",
                 ti.tm_hour, ti.tm_min,
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        oledMsg("NTP OK!", b);
    } else {
        oledMsg("NTP timeout", "Lanjut tanpa sinkron");
    }
    WiFi.mode(WIFI_OFF); delay(1100);
    return ok;
}

// ======================================================================
//  LOGIN
// ======================================================================
static void renderLogin() {
    oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
    oled.fillRect(0, 0, OLED_W, 9, WHITE);
    oled.setTextColor(BLACK); oled.setCursor(2, 1);
    oled.print(OS_HOST " OS " OS_VER " login");
    oled.setTextColor(WHITE);

    oled.setCursor(0, 14); oled.print("PIN:");
    uint8_t pinLen = (uint8_t)strlen(osPin);
    for (uint8_t i = 0; i < pinLen; i++)
        oled.print(i < loginLen ? "*" : "_");

    oled.setCursor(0, 30);
    if (loginTries > 0) {
        char b[22]; snprintf(b, sizeof(b), "Salah! Sisa %u coba", 3 - loginTries);
        oled.print(b);
    } else {
        oled.print("[D]=masuk  [B]=hapus");
    }
    oled.setCursor(0, 44); oled.print("3x salah = restart");
    oled.display();
}

static void procLogin(char key) {
    uint8_t pinLen = (uint8_t)strlen(osPin);
    if (key >= '0' && key <= '9' && loginLen < pinLen) {
        loginBuf[loginLen++] = key; loginBuf[loginLen] = '\0';
    } else if (key == 'B' && loginLen > 0) {
        loginBuf[--loginLen] = '\0';
    } else if (key == 'D') {
        if (strcmp(loginBuf, osPin) == 0) {
            loginTries = 0; loginLen = 0; loginBuf[0] = '\0';
            curSt = ST_TERM;
            tClear();
            DateTime now = rtc.now();
            char w[TB_COLS];
            snprintf(w, sizeof(w), "Login %02d:%02d %02u%s%04u",
                     now.hour(), now.minute(),
                     (unsigned)now.day(), MON[now.month()-1], (unsigned)now.year());
            tPrint(w);
            tPrint("Ketik 1 lalu D = help");
        } else {
            loginTries++; loginLen = 0; loginBuf[0] = '\0';
            if (loginTries >= 3) {
                oledMsg("ACCESS DENIED", "Restart 3s...");
                delay(3000); ESP.restart();
            }
        }
    }
}

// ======================================================================
//  SLEEP & WAKE
//  Tahan D untuk masuk sleep, tahan D lagi untuk bangun.
//  Wake pakai ssd1306_command(DISPLAYON), BUKAN oled.begin().
// ======================================================================
static void enterSleep() {
    oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
    oled.setCursor(12, 18); oled.print("FINIX OS sleeping");
    oled.setCursor(4,  34); oled.print("Tahan D 1.5s=bangun");
    oled.display();
    delay(900);
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
    holdTracking = false; holdFired = false; holdWasTap = false; holdStartMs = 0;
    curSt = ST_SLEEP;
}

static void wakeFromSleep() {
    oled.ssd1306_command(SSD1306_DISPLAYON);
    oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
    oled.setCursor(16, 24); oled.print("Bangun dari sleep");
    oled.display();
    delay(600);
    holdTracking = false; holdFired = false; holdWasTap = false; holdStartMs = 0;
    curSt = ST_TERM;
}

// ======================================================================
//  SHUTDOWN (halt)
// ======================================================================
static void doShutdown() {
    oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
    oled.setCursor(8,  18); oled.print("System halted.");
    oled.setCursor(4,  34); oled.print("Aman dimatikan.");
    oled.display();
    delay(1500);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
}

// ======================================================================
//  NEOFETCH
// ======================================================================
static void doNeofetch() {
    DateTime now = rtc.now();
    char b[TB_COLS];

    tPrint("  .--.");
    snprintf(b,sizeof(b),"  |    | %s@%s",OS_USER,OS_HOST); tPrint(b);
    tPrint("  '--'  --------");
    snprintf(b,sizeof(b),"OS   : FINIX %s", OS_VER);          tPrint(b);
    tPrint("HW   : ESP32 Classic");
    snprintf(b,sizeof(b),"CPU  : Xtensa %uMHz",ESP.getCpuFreqMHz()); tPrint(b);

    char k1[8], k2[8];
    fmtKB(k1, sizeof(k1), ESP.getFreeHeap());
    fmtKB(k2, sizeof(k2), ESP.getHeapSize());
    snprintf(b,sizeof(b),"RAM  : %s/%s", k1, k2);             tPrint(b);

    fmtKB(k1, sizeof(k1), ESP.getFlashChipSize());
    snprintf(b,sizeof(b),"Flash: %s", k1);                     tPrint(b);
    snprintf(b,sizeof(b),"Suhu : %.0fC", rtc.getTemperature()); tPrint(b);
    snprintf(b,sizeof(b),"Jam  : %02d:%02d:%02d",
             now.hour(), now.minute(), now.second());            tPrint(b);
    snprintf(b,sizeof(b),"Tgl  : %02u %s %04u",
             (unsigned)now.day(), MON[now.month()-1],
             (unsigned)now.year());                              tPrint(b);

    uint32_t up = (millis() - bootMs) / 1000;
    snprintf(b,sizeof(b),"Up   : %uh%02um%02us",
             (unsigned)(up/3600),
             (unsigned)((up%3600)/60),
             (unsigned)(up%60));                                 tPrint(b);
}

// ======================================================================
//  KALKULATOR
// ======================================================================
static struct {
    char   disp[16];
    double acc;
    char   op;
    bool   opPending;
    bool   dotUsed;
    bool   sciMode;
} CAL;

static void calReset() {
    strncpy(CAL.disp, "0", sizeof(CAL.disp));
    CAL.acc = 0; CAL.op = 0;
    CAL.opPending = false; CAL.dotUsed = false; CAL.sciMode = false;
}

static void calEval() {
    if (!CAL.opPending) return;
    double b = atof(CAL.disp);
    switch (CAL.op) {
        case '+': CAL.acc += b; break;
        case '-': CAL.acc -= b; break;
        case 'x': CAL.acc *= b; break;
        case '/': CAL.acc  = (b != 0) ? CAL.acc / b : 0; break;
    }
    snprintf(CAL.disp, sizeof(CAL.disp), "%.7g", CAL.acc);
    CAL.opPending = false; CAL.dotUsed = false;
}

static void renderCalc() {
    oled.clearDisplay();
    drawHeader("KALKULATOR");
    if (CAL.op) {
        oled.setCursor(120, 1); oled.setTextColor(BLACK);
        oled.print(CAL.op); oled.setTextColor(WHITE);
    }

    oled.setTextSize(2); oled.setTextColor(WHITE);
    uint8_t dlen = (uint8_t)strlen(CAL.disp);
    const char* ds = (dlen > 9) ? CAL.disp + dlen - 9 : CAL.disp;
    int16_t x1, y1; uint16_t tw, th;
    oled.getTextBounds(ds, 0, 0, &x1, &y1, &tw, &th);
    oled.setCursor(OLED_W - tw - 2, 12); oled.print(ds);

    oled.setTextSize(1); oled.setTextColor(WHITE);
    if (!CAL.sciMode) {
        oled.setCursor(0, 34); oled.print("0-9=digit B=. A=+/-");
        oled.setCursor(0, 44); oled.print("#=op(+-x/) D== *=SCI");
        oled.setCursor(0, 54); oled.print("C=reset/keluar");
    } else {
        oled.setCursor(0, 34); oled.print("1=sin 2=cos 3=tan");
        oled.setCursor(0, 44); oled.print("4=sqrt 5=x2 6=1/x");
        oled.setCursor(0, 54); oled.print("7=log 8=ln  C=keluar");
    }
    oled.display();
}

static void procCalc(char key) {
    if (key == 'C') {
        if (CAL.sciMode) { CAL.sciMode = false; return; }
        // Tekan C di mode standar saat kalkulator kosong/baru = keluar ke terminal
        if (strcmp(CAL.disp, "0") == 0 && !CAL.opPending) {
            curSt = ST_TERM; tPrint("Keluar kalkulator.");
            return;
        }
        calReset(); return;
    }
    if (CAL.sciMode) {
        double v = atof(CAL.disp), r = 0; bool ok = true;
        switch (key) {
            case '1': r=sin(v*PI/180); break; case '2': r=cos(v*PI/180); break;
            case '3': r=tan(v*PI/180); break; case '4': r=v>=0?sqrt(v):0; break;
            case '5': r=v*v;           break; case '6': r=v!=0?1.0/v:0;  break;
            case '7': r=v>0?log10(v):0;break; case '8': r=v>0?log(v):0;  break;
            default: ok=false;
        }
        if (ok) { snprintf(CAL.disp,sizeof(CAL.disp),"%.7g",r); CAL.sciMode=false; }
        return;
    }
    if (key >= '0' && key <= '9') {
        uint8_t dl = (uint8_t)strlen(CAL.disp);
        if (dl < 13) {
            if (strcmp(CAL.disp,"0")==0) { CAL.disp[0]=key; CAL.disp[1]='\0'; }
            else { CAL.disp[dl]=key; CAL.disp[dl+1]='\0'; }
        }
        return;
    }
    if (key=='B' && !CAL.dotUsed) {
        uint8_t dl=(uint8_t)strlen(CAL.disp);
        if (dl<13){CAL.disp[dl]='.';CAL.disp[dl+1]='\0';CAL.dotUsed=true;} return;
    }
    if (key=='A') { double v=-atof(CAL.disp); snprintf(CAL.disp,sizeof(CAL.disp),"%.7g",v); return; }
    if (key=='*') { CAL.sciMode=true; return; }
    if (key=='#') {
        static uint8_t oi=0; static const char ops[]="+-x/";
        if (CAL.opPending) calEval();
        CAL.acc=atof(CAL.disp); CAL.op=ops[oi++%4];
        strncpy(CAL.disp,"0",sizeof(CAL.disp));
        CAL.opPending=true; CAL.dotUsed=false; return;
    }
    if (key=='D') { calEval(); return; }
}

// ======================================================================
//  GAME: SNAKE
// ======================================================================
#define SN_MAX 48
#define SN_CS  4
#define SN_GW  (OLED_W / SN_CS)
#define SN_GH  ((OLED_H - 10) / SN_CS)

static struct {
    int8_t  bx[SN_MAX], by[SN_MAX];
    uint8_t len;
    int8_t  dx, dy;
    int8_t  fx, fy;
    bool    alive;
    uint16_t score;
    uint32_t lastMoveMs;
    uint16_t speedMs;
} SNK;

static void snakeFood() {
    for (uint8_t t = 0; t < 200; t++) {
        int8_t nx = random(0, SN_GW), ny = random(0, SN_GH);
        bool ok = true;
        for (uint8_t i = 0; i < SNK.len; i++)
            if (SNK.bx[i]==nx && SNK.by[i]==ny) { ok=false; break; }
        if (ok) { SNK.fx=nx; SNK.fy=ny; return; }
    }
}

static void snakeInit() {
    memset(&SNK, 0, sizeof(SNK));
    SNK.len=3; SNK.dx=1; SNK.alive=true; SNK.speedMs=300; SNK.lastMoveMs=millis();
    for (uint8_t i=0;i<3;i++) { SNK.bx[i]=4-i; SNK.by[i]=5; }
    snakeFood();
}

static void renderSnake() {
    oled.clearDisplay();
    drawHeader("SNAKE");
    char sc[10]; snprintf(sc,sizeof(sc),"Skor:%u",SNK.score);
    oled.setCursor(70,1); oled.setTextColor(BLACK); oled.print(sc); oled.setTextColor(WHITE);

    for (uint8_t i=0;i<SNK.len;i++)
        oled.fillRect(SNK.bx[i]*SN_CS, 10+SNK.by[i]*SN_CS, SN_CS-1, SN_CS-1, WHITE);
    oled.drawRect(SNK.fx*SN_CS+1, 10+SNK.fy*SN_CS+1, SN_CS-2, SN_CS-2, WHITE);

    if (!SNK.alive) {
        oled.fillRect(14,22,100,20,BLACK); oled.drawRect(14,22,100,20,WHITE);
        oled.setCursor(22,26); oled.print("GAME OVER");
        oled.setCursor(16,34); oled.print("D=ulang C=keluar");
    } else {
        oled.setCursor(0,57); oled.print("2/4/6/8   C=keluar");
    }
    oled.display();
}

static void procSnake(char key) {
    if (!SNK.alive) {
        if (key=='D') snakeInit();
        else if (key=='C') { curSt=ST_TERM; tPrint("Snake selesai."); }
        return;
    }
    if (key=='2'&&SNK.dy!=1)  { SNK.dx=0;  SNK.dy=-1; }
    else if (key=='8'&&SNK.dy!=-1) { SNK.dx=0;  SNK.dy=1;  }
    else if (key=='4'&&SNK.dx!=1)  { SNK.dx=-1; SNK.dy=0;  }
    else if (key=='6'&&SNK.dx!=-1) { SNK.dx=1;  SNK.dy=0;  }
    else if (key=='C') { curSt=ST_TERM; tPrint("Keluar snake."); }
}

static void updateSnake() {
    if (!SNK.alive) return;
    if (millis() - SNK.lastMoveMs < SNK.speedMs) return;
    SNK.lastMoveMs = millis();

    int8_t nx = (SNK.bx[0]+SNK.dx+SN_GW) % SN_GW;
    int8_t ny = (SNK.by[0]+SNK.dy+SN_GH) % SN_GH;

    for (uint8_t i=0;i<SNK.len-1;i++)
        if (SNK.bx[i]==nx && SNK.by[i]==ny) { SNK.alive=false; return; }

    for (uint8_t i=SNK.len-1;i>0;i--) { SNK.bx[i]=SNK.bx[i-1]; SNK.by[i]=SNK.by[i-1]; }
    SNK.bx[0]=nx; SNK.by[0]=ny;

    if (nx==SNK.fx && ny==SNK.fy) {
        if (SNK.len < SN_MAX-1) SNK.len++;
        SNK.score += 10;
        if (SNK.speedMs > 100) SNK.speedMs -= 8;
        snakeFood();
    }
}

// ======================================================================
//  GAME: DINO JUMP
// ======================================================================
#define JMP_GROUND 54

static struct {
    float   posY, velY;
    bool    onGround;
    uint8_t obsX, obsH;
    uint16_t score;
    bool    alive;
    uint32_t lastFrameMs;
    uint8_t  speed;
} JMP;

static void jumpInit() {
    JMP.posY      = JMP_GROUND - 10.0f;
    JMP.velY      = 0; JMP.onGround = true;
    JMP.obsX      = OLED_W - 1;
    JMP.obsH      = 10 + random(0, 12);
    JMP.score     = 0; JMP.alive = true;
    JMP.lastFrameMs = millis(); JMP.speed = 2;
}

static void renderJump() {
    oled.clearDisplay();
    drawHeader("DINO JUMP");
    char sc[10]; snprintf(sc,sizeof(sc),"Skor:%u",JMP.score);
    oled.setCursor(70,1); oled.setTextColor(BLACK); oled.print(sc); oled.setTextColor(WHITE);

    oled.drawFastHLine(0, JMP_GROUND+1, OLED_W, WHITE);
    oled.drawRect(8, (uint8_t)JMP.posY, 10, 10, WHITE);
    oled.fillRect(JMP.obsX, JMP_GROUND+1-JMP.obsH, 7, JMP.obsH, WHITE);

    if (!JMP.alive) {
        oled.fillRect(14,20,100,22,BLACK); oled.drawRect(14,20,100,22,WHITE);
        oled.setCursor(22,24); oled.print("GAME OVER");
        oled.setCursor(16,34); oled.print("D=ulang C=keluar");
    } else {
        oled.setCursor(0,57); oled.print("D=lompat    C=keluar");
    }
    oled.display();
}

static void procJump(char key) {
    if (!JMP.alive) {
        if (key=='D') jumpInit();
        else if (key=='C') { curSt=ST_TERM; tPrint("Dino selesai."); }
        return;
    }
    if ((key=='D'||key=='*'||key=='2') && JMP.onGround) {
        JMP.velY = -4.6f; JMP.onGround = false;   // FIX8: dulu -7.8 (lompat kelewat tinggi/jauh)
    }
    if (key=='C') { curSt=ST_TERM; tPrint("Keluar dino."); }
}

static void updateJump() {
    if (!JMP.alive) return;
    if (millis() - JMP.lastFrameMs < 32) return;
    JMP.lastFrameMs = millis();

    JMP.velY += 0.55f;   // FIX8: gravitasi dinaikkan agar lompatan lebih singkat & pas
    JMP.posY += JMP.velY;
    if (JMP.posY >= (float)(JMP_GROUND - 10)) {
        JMP.posY = (float)(JMP_GROUND - 10); JMP.velY = 0; JMP.onGround = true;
    }

    if (JMP.obsX > JMP.speed) {
        JMP.obsX -= JMP.speed;
    } else {
        JMP.obsX  = OLED_W - 1;
        JMP.obsH  = 10 + random(0, 14);
        JMP.score++;
        if (JMP.score % 5 == 0 && JMP.speed < 7) JMP.speed++;
    }

    uint8_t cy = (uint8_t)JMP.posY;
    if (JMP.obsX < 18 && JMP.obsX + 7 > 8) {
        if (cy + 10 > JMP_GROUND + 1 - JMP.obsH) JMP.alive = false;
    }
}

// ======================================================================
//  GAME: PONG
// ======================================================================
#define PG_TOP  10
#define PG_H    (OLED_H-PG_TOP)
#define PG_PDH  14
#define PG_PDW   3

static struct {
    float   bx, by, vx, vy;
    uint8_t p1y, p2y;
    uint8_t score1, score2;
    bool    alive;
    uint32_t lastFrameMs;
} PNG;

static void pongInit() {
    PNG.bx=OLED_W/2; PNG.by=PG_H/2; PNG.vx=2.5f; PNG.vy=1.5f;
    PNG.p1y=PNG.p2y=PG_H/2-PG_PDH/2;
    PNG.score1=0; PNG.score2=0; PNG.alive=true; PNG.lastFrameMs=millis();
}

static void renderPong() {
    oled.clearDisplay();
    oled.fillRect(0,0,OLED_W,PG_TOP-1,WHITE);
    oled.setTextColor(BLACK); oled.setTextSize(1);
    oled.setCursor(2,1); oled.print("PONG");
    char sc[16]; snprintf(sc,sizeof(sc),"Kamu:%u  AI:%u",PNG.score1,PNG.score2);
    oled.setCursor(40,1); oled.print(sc);
    oled.setTextColor(WHITE);

    for (uint8_t y=PG_TOP;y<OLED_H;y+=4) oled.drawPixel(OLED_W/2,y,WHITE);
    oled.fillRect(2,        PG_TOP+PNG.p1y, PG_PDW, PG_PDH, WHITE);
    oled.fillRect(OLED_W-PG_PDW-2, PG_TOP+PNG.p2y, PG_PDW, PG_PDH, WHITE);
    oled.fillRect((uint8_t)PNG.bx, PG_TOP+(uint8_t)PNG.by, 3, 3, WHITE);

    if (!PNG.alive) {
        bool menang = (PNG.score1 > PNG.score2);
        oled.fillRect(14,22,100,22,BLACK); oled.drawRect(14,22,100,22,WHITE);
        oled.setCursor(menang?16:22, 26); oled.print(menang?"KAMU MENANG!":"AI MENANG!");
        oled.setCursor(16,38); oled.print("D=ulang  C=keluar");
    } else {
        oled.setCursor(0,57); oled.print("2=atas 8=bawah C=kel");
    }
    oled.display();
}

static void procPong(char key) {
    if (!PNG.alive) {
        if (key=='D') pongInit();
        else if (key=='C') { curSt=ST_TERM; tPrint("Pong selesai."); }
        return;
    }
    if (key=='2' && PNG.p1y > 0)          PNG.p1y -= 3;
    if (key=='8' && PNG.p1y < PG_H-PG_PDH) PNG.p1y += 3;
    if (key=='C') { curSt=ST_TERM; tPrint("Keluar pong."); }
}

static void updatePong() {
    if (!PNG.alive) return;
    if (millis() - PNG.lastFrameMs < 28) return;
    PNG.lastFrameMs = millis();

    PNG.bx += PNG.vx; PNG.by += PNG.vy;

    if (PNG.by <= 0)        { PNG.by=0;       PNG.vy=-PNG.vy; }
    if (PNG.by >= PG_H-3)   { PNG.by=PG_H-3;  PNG.vy=-PNG.vy; }

    float aiCenter = PNG.p2y + PG_PDH/2.0f;
    if (aiCenter < PNG.by+1.5f && PNG.p2y < PG_H-PG_PDH) PNG.p2y += 2;
    else if (aiCenter > PNG.by+1.5f && PNG.p2y > 0)       PNG.p2y -= 2;

    if (PNG.bx<=5 && PNG.by+3>=PNG.p1y && PNG.by<=PNG.p1y+PG_PDH) {
        PNG.bx=5; PNG.vx=fabsf(PNG.vx);
        PNG.vy += (PNG.by-(PNG.p1y+PG_PDH/2.0f))*0.08f;
    }
    if (PNG.bx>=OLED_W-PG_PDW-5 && PNG.by+3>=PNG.p2y && PNG.by<=PNG.p2y+PG_PDH) {
        PNG.bx=OLED_W-PG_PDW-5; PNG.vx=-fabsf(PNG.vx);
    }

    if (PNG.bx < 0)       { PNG.score2++; PNG.bx=OLED_W/2; PNG.by=PG_H/2; PNG.vx= 2.5f; PNG.vy=1.5f; }
    if (PNG.bx >= OLED_W) { PNG.score1++; PNG.bx=OLED_W/2; PNG.by=PG_H/2; PNG.vx=-2.5f; PNG.vy=1.5f; }
    if (PNG.score1>=5 || PNG.score2>=5) PNG.alive=false;
}

// ======================================================================
//  MODE JAM DIGITAL PENUH  (FIX9 - menu baru sesuai permintaan)
//  Layout 128x64:
//    - Jam:menit:detik besar di tengah atas (textSize 3)
//    - Garis pemisah
//    - Hari, tanggal di bawah (textSize 1)
//    - Petunjuk "C=keluar" di baris paling bawah
//  Render dibatasi tiap 200ms (bukan tiap loop) agar detik tidak
//  "flicker" dan CPU tidak sibuk redraw berulang tanpa perlu.
// ======================================================================
static uint32_t clockLastDrawMs = 0;

static void renderClock() {
    DateTime now = rtc.now();

    oled.clearDisplay();
    oled.setTextColor(WHITE);

    // -- Jam:menit:detik besar, di tengah ---------------------------------
    char hms[10];
    snprintf(hms, sizeof(hms), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    oled.setTextSize(2);
    int16_t x1, y1; uint16_t tw, th;
    oled.getTextBounds(hms, 0, 0, &x1, &y1, &tw, &th);
    oled.setCursor((OLED_W - tw) / 2, 14);
    oled.print(hms);

    // -- Garis pemisah ------------------------------------------------------
    oled.drawFastHLine(4, 38, OLED_W - 8, WHITE);

    // -- Hari & tanggal -------------------------------------------------------
    char dateLine[24];
    snprintf(dateLine, sizeof(dateLine), "%s, %02u %s %04u",
             WDAY[now.dayOfTheWeek()], (unsigned)now.day(),
             MON[now.month() - 1], (unsigned)now.year());
    oled.setTextSize(1);
    oled.getTextBounds(dateLine, 0, 0, &x1, &y1, &tw, &th);
    oled.setCursor((OLED_W - tw) / 2, 46);
    oled.print(dateLine);

    // -- Petunjuk keluar -------------------------------------------------------
    oled.setCursor(40, 57);
    oled.print("C = keluar");

    oled.display();
}

static void procClock(char key) {
    if (key == 'C') {
        curSt = ST_TERM;
        tPrint("Keluar mode jam.");
    }
}

// ======================================================================
//  MENU NOMOR -> NAMA FUNGSI  (FIX1)
//  Cukup ketik nomor di terminal lalu tekan D. Tabel ini SATU-SATUNYA
//  sumber kebenaran nomor menu, dipakai baik oleh help maupun execCmd,
//  agar daftar bantuan tidak pernah berbeda dari yang benar-benar berjalan.
// ======================================================================
struct MenuItem { uint8_t num; const char* name; const char* desc; };
static const MenuItem MENU[] = {
    { 1,  "help",   "daftar menu ini"   },
    { 2,  "about",  "info OS"           },
    { 3,  "neo",    "neofetch"          },
    { 4,  "date",   "jam & tanggal"     },
    { 5,  "up",     "uptime"            },
    { 6,  "cls",    "bersihkan layar"   },
    { 7,  "calc",   "kalkulator"        },
    { 8,  "snake",  "game ular"         },
    { 9,  "jump",   "game dino"         },
    { 10, "pong",   "game pimpong"      },
    { 11, "sleep",  "mode tidur"        },
    { 12, "reboot", "restart"           },
    { 13, "halt",   "shutdown"          },
    { 14, "clock",  "jam digital penuh" },
};
#define MENU_N (sizeof(MENU)/sizeof(MENU[0]))


// ======================================================================
//  EKSEKUSI PERINTAH TERMINAL  (FIX1: nomor menu, bukan teks)
// ======================================================================
static void execCmd() {
    if (cmdLen == 0) return;

    uint16_t n = (uint16_t)atoi(cmdBuf);

    // Echo: tampilkan nomor yang dijalankan di output
    char echo[16];
    snprintf(echo, sizeof(echo), "%s> %u", OS_HOST, n);
    tPrint(echo);

    switch (n) {
        case 1: { // help
            tPrint("Menu (ketik nomor+D):");
            for (uint8_t i = 0; i < MENU_N; i++) {
                char b[TB_COLS];
                snprintf(b, sizeof(b), "%2u %-7s %s", MENU[i].num, MENU[i].name, MENU[i].desc);
                tPrint(b);
            }
            break;
        }
        case 2: { // about
            char b[TB_COLS];
            tPrint("FINIX OS " OS_VER);
            tPrint("The Micro Computer");
            tPrint("ESP32+OLED+RTC+Keypad");
            snprintf(b,sizeof(b),"By: %s",OS_AUTHOR); tPrint(b);
            tPrint("Build: " __DATE__);
            break;
        }
        case 3: doNeofetch(); break; // neo
        case 4: { // date
            DateTime now = rtc.now();
            char b[TB_COLS];
            snprintf(b,sizeof(b),"%02d:%02d:%02d %s",
                     now.hour(),now.minute(),now.second(),WDAY[now.dayOfTheWeek()]);
            tPrint(b);
            snprintf(b,sizeof(b),"%02u %s %04u",
                     (unsigned)now.day(),MON[now.month()-1],(unsigned)now.year());
            tPrint(b);
            break;
        }
        case 5: { // up (uptime)
            uint32_t up=(millis()-bootMs)/1000;
            char b[TB_COLS];
            snprintf(b,sizeof(b),"Uptime: %uh%02um%02us",
                     (unsigned)(up/3600),(unsigned)((up%3600)/60),(unsigned)(up%60));
            tPrint(b);
            break;
        }
        case 6: tClear(); break;                       // cls
        case 7: calReset(); curSt=ST_CALC; break;       // calc
        case 8: snakeInit(); curSt=ST_SNAKE; break;      // snake
        case 9: jumpInit();  curSt=ST_JUMP;  break;      // jump
        case 10: pongInit(); curSt=ST_PONG;  break;      // pong
        case 11: enterSleep(); break;                    // sleep
        case 12:                                         // reboot
            oledMsg("Rebooting...","");
            delay(700); ESP.restart();
            break;
        case 13: doShutdown(); break;                    // halt
        case 14: curSt = ST_CLOCK; clockLastDrawMs = 0; break; // clock
        default: {
            char b[TB_COLS];
            snprintf(b,sizeof(b),"'%u': nomor tak ada.",n);
            tPrint(b); tPrint("Ketik 1 lalu D = help");
            break;
        }
    }

    cmdLen=0; cmdBuf[0]='\0';
}

// ======================================================================
//  KEYPAD HANDLER TERMINAL
//  FIX1: hanya digit 0-9, B=hapus, C=clear input, D=jalankan.
//  Scroll: * naik, # turun -- hanya saat input kosong.
//  Tombol A tidak lagi dipakai (dulu untuk toggle T9, sekarang tak perlu).
// ======================================================================
static void termKey(char key) {
    if (key == 'B') {
        if (cmdLen>0) { cmdBuf[--cmdLen]='\0'; }
        return;
    }
    if (key == 'C') {
        cmdLen=0; cmdBuf[0]='\0'; return;
    }

    // Tombol * dan # — scroll jika input kosong
    if (key == '*') {
        if (cmdLen==0) {
            int8_t maxScroll = (int8_t)(tbCount>7 ? tbCount-7 : 0);
            if (tbScroll < maxScroll) tbScroll++;
        }
        return;
    }
    if (key == '#') {
        if (cmdLen==0) {
            if (tbScroll > 0) tbScroll--;
        }
        return;
    }

    // Tombol D = ENTER (jalankan nomor menu)
    // Catatan: D sudah ditangani oleh mekanisme hold di loop()
    // termKey() hanya dipanggil jika D adalah tap (bukan hold)
    if (key == 'D') { execCmd(); return; }

    // Hanya digit 0-9 yang diterima sebagai nomor menu
    if (key < '0' || key > '9') return;
    if (cmdLen < CMD_MAX) { cmdBuf[cmdLen++]=key; cmdBuf[cmdLen]='\0'; }
}

// ======================================================================
//  SETUP
// ======================================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[FINIX OS " OS_VER "] boot start");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[FATAL] OLED tidak ada!"); while(true) delay(500);
    }
    oled.clearDisplay(); oled.display();
    Serial.println("[OK] OLED");

    if (!rtc.begin()) {
        oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
        oled.setCursor(0,14); oled.print("! RTC tidak ada");
        oled.setCursor(0,28); oled.print("Cek SDA21/SCL22");
        oled.display();
        Serial.println("[FATAL] RTC!");
        while(true) delay(500);
    }
    Serial.println("[OK] RTC DS3231");

    kpad.setDebounceTime(50);
    randomSeed(analogRead(34));

    showBoot();

    if (!rtcIsValid()) {
        Serial.println("[RTC] invalid, sync NTP...");
        bool synced = false;
        for (uint8_t i=0; i<3 && !synced; i++) synced = doNTP();
        if (!synced) Serial.println("[NTP] semua retry gagal.");
    } else {
        DateTime n = rtc.now();
        Serial.printf("[RTC] valid: %02d:%02d:%02d %02d/%02d/%04d\n",
                      n.hour(),n.minute(),n.second(),n.day(),n.month(),n.year());
    }

    calReset();
    tClear();
    bootMs = millis();

    holdStartMs  = 0;
    holdTracking = false;
    holdFired    = false;
    holdWasTap   = false;

    curSt = ST_LOGIN;
    loginLen=0; loginBuf[0]='\0'; loginTries=0;

    Serial.println("[OK] FINIX OS siap. Masukkan PIN.");
}

// ======================================================================
//  LOOP — satu getKey() per loop, tidak ada konflik getState()
// ======================================================================
void loop() {
    uint32_t nowMs = millis();

    char key = kpad.getKey();

    // -- Deteksi tahan D untuk sleep (di terminal) --------------------
    if (curSt == ST_TERM) {
        if (key == 'D') {
            holdStartMs  = nowMs;
            holdTracking = true;
            holdFired    = false;
            holdWasTap   = false;
            key = NO_KEY;  // konsumsi event, ditangani di blok hold tracking
        }

        if (holdTracking && !holdFired) {
            if (kpad.isPressed('D')) {
                if (nowMs - holdStartMs >= HOLD_MS) {
                    holdFired = true;
                    holdTracking = false;
                    tPrint("Sleep mode...");
                    enterSleep();
                }
            } else {
                // D dilepas sebelum HOLD_MS = tap biasa = ENTER
                holdTracking = false;
                if (!holdWasTap) {
                    holdWasTap = true;
                    execCmd();
                }
            }
        }
    }

    // -- Deteksi tahan D untuk BANGUN dari sleep -----------------------
    if (curSt == ST_SLEEP) {
        if (key == 'D') {
            holdStartMs  = nowMs;
            holdTracking = true;
            holdFired    = false;
            key = NO_KEY;
        }
        if (holdTracking && !holdFired) {
            if (kpad.isPressed('D')) {
                if (nowMs - holdStartMs >= HOLD_MS) {
                    holdFired    = true;
                    holdTracking = false;
                    wakeFromSleep();
                }
            } else {
                holdTracking = false;
            }
        }
    }

    // -- Proses keypad berdasarkan state ---------------------------------
    if (key != NO_KEY) {
        switch (curSt) {
            case ST_LOGIN: procLogin(key); break;
            case ST_TERM:  termKey(key);   break;
            case ST_CALC:  procCalc(key);  break;
            case ST_SNAKE: procSnake(key); break;
            case ST_JUMP:  procJump(key);  break;
            case ST_PONG:  procPong(key);  break;
            case ST_CLOCK: procClock(key); break;
            case ST_SLEEP: /* semua tombol selain D diabaikan */ break;
            default: break;
        }
    }

    // -- Update game (non-blocking) ---------------------------------------
    if (curSt == ST_SNAKE) updateSnake();
    if (curSt == ST_JUMP)  updateJump();
    if (curSt == ST_PONG)  updatePong();

    // -- Render --------------------------------------------------------------
    switch (curSt) {
        case ST_LOGIN: renderLogin(); break;
        case ST_TERM:  renderTerm();  break;
        case ST_CALC:  renderCalc();  break;
        case ST_SNAKE: renderSnake(); break;
        case ST_JUMP:  renderJump();  break;
        case ST_PONG:  renderPong();  break;
        case ST_CLOCK:
            if (nowMs - clockLastDrawMs >= 200) {
                clockLastDrawMs = nowMs;
                renderClock();
            }
            break;
        case ST_SLEEP: /* layar mati, tidak render */ break;
        default: break;
    }
}
