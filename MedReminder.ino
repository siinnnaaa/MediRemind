#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <TouchScreen.h>
#include <EEPROM.h>
#include <Wire.h>
#include <RTClib.h>

#define YP A3
#define XM A2
#define YM 9
#define XP 8

#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define WHITE   0xFFFF
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define GREY    0x7BEF
#define VIOLET  0xA81F
#define ORANGE  0xFBE0

#define MINPRESSURE 10
#define MAXPRESSURE 1200

#define BUZZER_PIN 53

// کالیبراسیون تأییدشده
#define TS_MINX 181
#define TS_MAXX 938
#define TS_MINY 149
#define TS_MAXY 954

// EEPROM: یک بایت magic برای نسخه‌بندی + آرایه‌ی meds
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_MAGIC_VAL  0xC4   // اگه struct عوض شد، این مقدار رو تغییر بده تا ریست بشه
#define EEPROM_MEDS_ADDR  4

MCUFRIEND_kbv tft;
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
RTC_DS3231 rtc;

// پالت رنگ داروها (RGB565)
const uint16_t medColors[6] = { RED, GREEN, BLUE, YELLOW, MAGENTA, ORANGE };
#define COLOR_COUNT 6

// ساختار داده‌ی دارو
struct Medicine {
  bool active;
  char name[12];
  uint8_t colorIndex;
  uint8_t doseCount;
  char times[3][6];
  uint8_t takenDoses;
};

#define MED_COUNT 5
Medicine meds[MED_COUNT];

enum Screen {
  SCREEN_MAIN,
  SCREEN_MED,
  SCREEN_KEYBOARD,
  SCREEN_SCHEDULE,
  SCREEN_ALARM,  
  SCREEN_TIME_SET    // جدید
};

Screen currentScreen = SCREEN_MAIN;
int    selectedMed   = 0;
int tempHH = 0, tempMM = 0; 
int scheduleStep = 0;
int schedJY = 0, schedJM = 0, schedJD = 0;  // <-- این خط را اضافه کن


// کیبورد
bool   kbCaps = false;
String kbInput = "";

String kbSymbol[3][10] = {
  {"Q","W","E","R","T","Y","U","I","O","P"},
  {"A","S","D","F","G","H","J","K","L",";"},
  {"Z","X","C","V","B","N","M",".","<",""}
};

// آلارم
int    alarmMed     = -1;   // کدام دارو آلارم دارد
int    alarmTimeIdx = -1;   // کدام دوز
int    lastTrigMin  = -1;   // جلوگیری از تکرار در همان دقیقه
uint8_t lastResetDay = 0;   // ریست شمارنده روزانه
int8_t lastClockMin = -1;   // ضد چشمک ساعت کوچک

// ملودی غیرمسدودکننده
const int MEL_NOTE[] = {659, 587, 523, 587, 659, 659, 659};
const int MEL_DUR[]  = {200, 200, 200, 200, 200, 200, 400};
#define MEL_LEN 7
uint8_t       melStep  = 0;
unsigned long melTimer = 0;

// ==================== تاچ ====================
bool getTouchXY(int &tx, int &ty) {
  TSPoint p = ts.getPoint();
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);
  if (p.z < MINPRESSURE || p.z > MAXPRESSURE) return false;
  tx = map(p.y, TS_MAXY, TS_MINY, 0, tft.width());
  ty = map(p.x, TS_MAXX, TS_MINX, 0, tft.height());
  return true;
}

// ==================== EEPROM ====================
void saveMeds() {
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
  EEPROM.put(EEPROM_MEDS_ADDR, meds);
}

void loadMeds() {
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic != EEPROM_MAGIC_VAL) {
    // اولین اجرا یا تغییر ساختار -> ریست
    for (int i = 0; i < MED_COUNT; i++) {
      memset(&meds[i], 0, sizeof(Medicine));
      strcpy(meds[i].name, "---");
      meds[i].colorIndex = i % COLOR_COUNT;
    }
    saveMeds();
    return;
  }
  EEPROM.get(EEPROM_MEDS_ADDR, meds);
  for (int i = 0; i < MED_COUNT; i++) {
    if (meds[i].name[0] < 32 || meds[i].name[0] > 126) {
      memset(&meds[i], 0, sizeof(Medicine));
      strcpy(meds[i].name, "---");
    }
    if (meds[i].colorIndex >= COLOR_COUNT) meds[i].colorIndex = 0;
  }
}
// ==================== کمکی زمان ====================
int parseHH(const char* t) { return (t[0]-'0')*10 + (t[1]-'0'); }
int parseMM(const char* t) { return (t[3]-'0')*10 + (t[4]-'0'); }
// ==================== تبدیل میلادی به شمسی ====================
void gregorianToJalali(int gy, int gm, int gd, int &jy, int &jm, int &jd) {
  static const int g_d_m[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  int gy2 = (gm > 2) ? (gy + 1) : gy;
  long days = 355666L + (365L*gy) + ((gy2+3)/4) - ((gy2+99)/100)
              + ((gy2+399)/400) + gd + g_d_m[gm-1];
  jy = -1595 + (33 * (days / 12053));
  days %= 12053;
  jy += 4 * (days / 1461);
  days %= 1461;
  if (days > 365) {
    jy += (days - 1) / 365;
    days = (days - 1) % 365;
  }
  if (days < 186) {
    jm = 1 + (days / 31);
    jd = 1 + (days % 31);
  } else {
    jm = 7 + ((days - 186) / 30);
    jd = 1 + ((days - 186) % 30);
  }
}
// ==================== تاریخ شمسی صفحه اصلی ====================
int8_t lastClockDay = -1;  // ضد چشمک تاریخ

void drawMiniDate(bool force) {
  DateTime now = rtc.now();
  if (!force && now.day() == lastClockDay) return;
  lastClockDay = now.day();

  int jy, jm, jd;
  gregorianToJalali(now.year(), now.month(), now.day(), jy, jm, jd);

  tft.fillRect(190, 8, 60, 12, BLACK);   // زیر/کنار ساعت
  tft.setTextColor(CYAN);
  tft.setTextSize(1);
  tft.setCursor(190, 8);
  tft.print(jy); tft.print('/');
  if (jm < 10) tft.print('0');
  tft.print(jm); tft.print('/');
  if (jd < 10) tft.print('0');
  tft.print(jd);
}



// ==================== ساعت کوچک صفحه اصلی ====================
void drawMiniClock(bool force) {
  DateTime now = rtc.now();
  if (!force && now.minute() == lastClockMin) return;
  lastClockMin = now.minute();

  tft.fillRect(255, 5, 65, 18, BLACK);
  tft.setTextColor(GREEN);
  tft.setTextSize(2);
  tft.setCursor(255, 5);
  if (now.hour() < 10) tft.print('0');
  tft.print(now.hour());
  tft.print(':');
  if (now.minute() < 10) tft.print('0');
  tft.print(now.minute());
}

// ==================== صفحه‌ی اصلی ====================
void drawMainScreen() {
  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print("Med Reminder");

  for (int i = 0; i < MED_COUNT; i++) {
    int cy = 30 + i * 40;
    tft.drawRoundRect(2, cy, 316, 38, 4, CYAN);

    // مربع رنگ دارو
    if (meds[i].active)
      tft.fillRect(300, cy + 4, 12, 12, medColors[meds[i].colorIndex]);

    tft.setTextSize(2);
    tft.setTextColor(YELLOW);
    tft.setCursor(6, cy + 4);
    tft.print(i + 1);
    tft.print(":");

    if (meds[i].active) {
      tft.print(meds[i].name);

      // Taken در خط اول، سمت راست
      tft.setTextSize(1);
      tft.setTextColor(WHITE);
      tft.setCursor(210, cy + 4);
      tft.print("T:");
      tft.print(meds[i].takenDoses);
      tft.print("/");
      tft.print(meds[i].doseCount);

      // زمان‌ها در خط دوم
      tft.setTextSize(1);
      tft.setTextColor(WHITE);
      tft.setCursor(6, cy + 22);
      for (int t = 0; t < meds[i].doseCount && t < 3; t++) {
        tft.print(meds[i].times[t]);
        if (t < meds[i].doseCount - 1) tft.print(" ");
      }

      // نوار پیشرفت در خط دوم سمت راست
      int barW = 110;
      int barX = 200;
      int filled = (meds[i].doseCount > 0)
                   ? (barW * meds[i].takenDoses / meds[i].doseCount) : 0;
      tft.fillRect(barX, cy + 24, filled, 6, GREEN);
      tft.fillRect(barX + filled, cy + 24, barW - filled, 6, RED);
    } else {
      tft.setTextColor(GREY);
      tft.print(" (empty)");
    }
  }
  drawMiniClock(true);
  drawMiniDate(true);
}

void handleMainScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  // لمس ساعت کوچک گوشه -> تنظیم ساعت
  if (tx > 250 && ty < 25) {
    DateTime now = rtc.now();
    tempHH = now.hour();
    tempMM = now.minute();
    currentScreen = SCREEN_TIME_SET;
    drawTimeSetScreen();
    return;
  }

  for (int i = 0; i < MED_COUNT; i++) {
    int cy = 30 + i * 40;
    if (tx > 2 && tx < 318 && ty > cy && ty < cy + 38) {
      selectedMed   = i;
      currentScreen = SCREEN_MED;
      drawMedScreen();
      return;
    }
  }
}

// ==================== انتخاب رنگ ====================
#define SW_Y    165
#define SW_SIZE 45
#define SW_GAP  5
#define SW_X0   10

void drawColorPicker(uint8_t sel) {
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(SW_X0, 150);
  tft.print("Color:");
  for (uint8_t i = 0; i < COLOR_COUNT; i++) {
    int x = SW_X0 + i * (SW_SIZE + SW_GAP);
    tft.fillRect(x, SW_Y, SW_SIZE, SW_SIZE, medColors[i]);
    uint16_t b = (i == sel) ? WHITE : GREY;
    tft.drawRect(x, SW_Y, SW_SIZE, SW_SIZE, b);
    if (i == sel) tft.drawRect(x+1, SW_Y+1, SW_SIZE-2, SW_SIZE-2, b);
  }
}

int hitColorPicker(int x, int y) {
  if (y < SW_Y || y > SW_Y + SW_SIZE) return -1;
  for (uint8_t i = 0; i < COLOR_COUNT; i++) {
    int sx = SW_X0 + i * (SW_SIZE + SW_GAP);
    if (x > sx && x < sx + SW_SIZE) return i;
  }
  return -1;
}

// ==================== صفحه‌ی دارو ====================
void drawMedScreen() {
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setTextColor(CYAN);
  tft.setCursor(5, 5);
  tft.print("Medicine ");
  tft.print(selectedMed + 1);
  if (meds[selectedMed].active) {
    tft.setTextColor(YELLOW);
    tft.print(" - ");
    tft.print(meds[selectedMed].name);
  }

  tft.fillRoundRect(10,  40,  140, 45, 6, BLUE);
  tft.fillRoundRect(170, 40,  140, 45, 6, BLUE);
  tft.fillRoundRect(10,  100, 140, 45, 6, RED);
  tft.fillRoundRect(170, 100, 140, 45, 6, GREY);

  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(25,  55);  tft.print("Set Name");
  tft.setCursor(185, 55);  tft.print("Schedule");
  tft.setCursor(40,  115); tft.print("Delete");
  tft.setCursor(195, 115); tft.print("< Back");

  drawColorPicker(meds[selectedMed].colorIndex);
}

void handleMedScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  // انتخاب رنگ
  int ci = hitColorPicker(tx, ty);
  if (ci != -1) {
    meds[selectedMed].colorIndex = ci;
    saveMeds();
    drawColorPicker(ci);
    delay(150);
    return;
  }

  if (tx > 10 && tx < 150 && ty > 40 && ty < 85) {
    kbInput = "";
    kbCaps  = true;
    currentScreen = SCREEN_KEYBOARD;
    drawKeyboard();
  } else if (tx > 170 && tx < 310 && ty > 40 && ty < 85) {
    DateTime now = rtc.now();
    gregorianToJalali(now.year(), now.month(), now.day(), schedJY, schedJM, schedJD);  // <-- جدید
    currentScreen = SCREEN_SCHEDULE;
    drawScheduleScreen();
  } else if (tx > 10 && tx < 150 && ty > 100 && ty < 145) {
    memset(&meds[selectedMed], 0, sizeof(Medicine));
    strcpy(meds[selectedMed].name, "---");
    saveMeds();
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
  } else if (tx > 170 && tx < 310 && ty > 100 && ty < 145) {
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
  }
}
// ==================== کیبورد ====================
void drawKeyboard() {
  tft.fillScreen(BLACK);
  tft.drawRect(0, 0, 320, 45, WHITE);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(4, 12);
  tft.print(kbInput);

  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 10; col++) {
      String lbl = kbSymbol[row][col];
      if (lbl == "") continue;
      int bx = col * 32 + 1;
      int by = row * 40 + 50;
      uint16_t color = (row == 2 && col == 0) ? CYAN : YELLOW;
      tft.fillRoundRect(bx, by, 30, 35, 3, color);
      tft.setTextColor(BLACK);
      tft.setTextSize(2);
      tft.setCursor(bx + 7, by + 9);
      if (!kbCaps && lbl.length() == 1 && lbl[0] >= 'A' && lbl[0] <= 'Z')
        tft.print((char)(lbl[0] + 32));
      else
        tft.print(lbl);
    }
  }

  tft.fillRoundRect(1, 170, 190, 35, 3, VIOLET);
  tft.setTextColor(WHITE); tft.setTextSize(2);
  tft.setCursor(45, 182); tft.print("Space");

  tft.fillRoundRect(193, 170, 63, 35, 3, ORANGE);
  tft.setTextColor(BLACK); tft.setTextSize(2);
  tft.setCursor(198, 182); tft.print("<--");

  tft.fillRoundRect(1, 208, 95, 30, 3, kbCaps ? GREEN : GREY);
  tft.setTextColor(WHITE); tft.setTextSize(1);
  tft.setCursor(15, 218); tft.print(kbCaps ? "CAPS ON" : "caps");

  tft.fillRoundRect(98, 208, 95, 30, 3, CYAN);
  tft.setTextColor(BLACK); tft.setTextSize(2);
  tft.setCursor(110, 215); tft.print("OK");

  tft.fillRoundRect(195, 208, 123, 30, 3, RED);
  tft.setTextColor(WHITE); tft.setTextSize(2);
  tft.setCursor(210, 215); tft.print("Cancel");
}

void kbRefreshText() {
  tft.fillRect(0, 0, 320, 45, BLACK);
  tft.drawRect(0, 0, 320, 45, WHITE);
  tft.setTextSize(2); tft.setTextColor(WHITE);
  tft.setCursor(4, 12); tft.print(kbInput);
}

void handleKeyboard() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  if (ty >= 50 && ty < 170) {
    int row = (ty - 50) / 40;
    int col = tx / 32;
    if (row < 3 && col < 10) {
      String lbl = kbSymbol[row][col];
      if (lbl == "") return;
      if (kbInput.length() < 11) {
        if (!kbCaps && lbl.length() == 1 && lbl[0] >= 'A' && lbl[0] <= 'Z')
          kbInput += (char)(lbl[0] + 32);
        else
          kbInput += lbl;
        kbRefreshText();
      }
      return;
    }
  }

  if (ty >= 170 && ty < 205 && tx < 193) {
    if (kbInput.length() < 11) { kbInput += " "; kbRefreshText(); }
    return;
  }
  if (ty >= 170 && ty < 205 && tx >= 193) {
    if (kbInput.length() > 0) { kbInput.remove(kbInput.length()-1); kbRefreshText(); }
    return;
  }

  if (ty >= 208) {
    if (tx < 98) { kbCaps = !kbCaps; drawKeyboard(); return; }
    if (tx < 195) {
      if (kbInput.length() > 0) {
        kbInput.toCharArray(meds[selectedMed].name, 12);
        meds[selectedMed].active = true;
        saveMeds();
      }
      currentScreen = SCREEN_MED;
      drawMedScreen();
      return;
    }
    currentScreen = SCREEN_MED;
    drawMedScreen();
    return;
  }
}
// ==================== صفحه‌ی زمان‌بندی ====================

void drawScheduleScreen() {
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);

  if (scheduleStep == 0) {
    tft.setCursor(20, 10);
    tft.print("Doses per day?");
    tft.setTextSize(1);
    tft.setTextColor(CYAN);
    tft.setCursor(20, 35);
    tft.print("Date: ");
    tft.print(schedJY); tft.print('/');
    if (schedJM < 10) tft.print('0');
    tft.print(schedJM); tft.print('/');
    if (schedJD < 10) tft.print('0');
    tft.print(schedJD);
    tft.setTextSize(2);   // برگرداندن سایز

    for (int i = 1; i <= 3; i++) {
      tft.fillRoundRect(20 + (i-1)*100, 60, 80, 60, 6, BLUE);
      tft.setTextSize(3); tft.setTextColor(WHITE);
      tft.setCursor(50 + (i-1)*100, 78); tft.print(i);
    }
    tft.fillRoundRect(100, 170, 120, 40, 5, GREY);
    tft.setTextSize(2); tft.setTextColor(WHITE);
    tft.setCursor(125, 183); tft.print("< Back");
  } else {
    tft.setCursor(10, 5);
    tft.print("Dose "); tft.print(scheduleStep); tft.print(" time (HH:MM):");
    tft.drawRect(10, 35, 200, 35, WHITE);
    tft.setTextSize(2); tft.setTextColor(YELLOW);
    tft.setCursor(15, 46); tft.print(kbInput);

    int nums[] = {1,2,3,4,5,6,7,8,9,0};
    for (int i = 0; i < 10; i++) {
      int col = i % 5, row = i / 5;
      int bx = col * 60 + 10, by = row * 50 + 80;
      tft.fillRoundRect(bx, by, 50, 40, 4, BLUE);
      tft.setTextSize(2); tft.setTextColor(WHITE);
      tft.setCursor(bx + 16, by + 12); tft.print(nums[i]);
    }
    tft.fillRoundRect(10, 180, 50, 40, 4, GREEN);
    tft.setTextSize(2); tft.setTextColor(WHITE);
    tft.setCursor(24, 193); tft.print(":");
    tft.fillRoundRect(70, 180, 80, 40, 4, ORANGE);
    tft.setTextColor(BLACK); tft.setCursor(80, 193); tft.print("<--");
    tft.fillRoundRect(160, 180, 60, 40, 4, CYAN);
    tft.setTextColor(BLACK); tft.setCursor(173, 193); tft.print("OK");
    tft.fillRoundRect(230, 180, 85, 40, 4, RED);
    tft.setTextColor(WHITE); tft.setCursor(242, 193); tft.print("Cncl");
  }
}

void schRefreshText() {
  tft.fillRect(10, 35, 200, 35, BLACK);
  tft.drawRect(10, 35, 200, 35, WHITE);
  tft.setTextSize(2); tft.setTextColor(YELLOW);
  tft.setCursor(15, 46); tft.print(kbInput);
}

void handleScheduleScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  if (scheduleStep == 0) {
    if (ty > 60 && ty < 120) {
      for (int i = 1; i <= 3; i++) {
        if (tx > 20+(i-1)*100 && tx < 100+(i-1)*100) {
          meds[selectedMed].doseCount = i;
          meds[selectedMed].takenDoses = 0;
          scheduleStep = 1; kbInput = "";
          drawScheduleScreen();
          return;
        }
      }
    }
    if (ty > 170 && ty < 210 && tx > 100 && tx < 220) {
      scheduleStep = 0;
      currentScreen = SCREEN_MED;
      drawMedScreen();
    }
    return;
  }

  if (ty >= 80 && ty < 180) {
    int nums[] = {1,2,3,4,5,6,7,8,9,0};
    int row = (ty - 80) / 50;
    int col = (tx - 10) / 60;
    if (col >= 0 && col < 5 && row >= 0 && row < 2) {
      int idx = row * 5 + col;
      if (kbInput.length() < 5) { kbInput += String(nums[idx]); schRefreshText(); }
    }
    return;
  }

  if (ty >= 180) {
    if (tx < 60) {
      if (kbInput.length() < 5 && kbInput.indexOf(':') < 0) { kbInput += ":"; schRefreshText(); }
    } else if (tx < 155) {
      if (kbInput.length() > 0) { kbInput.remove(kbInput.length()-1); schRefreshText(); }
        } else if (tx < 225) {
      if (kbInput.length() == 5 && kbInput[2] == ':') {
        int hh = (kbInput[0]-'0')*10 + (kbInput[1]-'0');
        int mm = (kbInput[3]-'0')*10 + (kbInput[4]-'0');
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
          kbInput.toCharArray(meds[selectedMed].times[scheduleStep - 1], 6);
          if (scheduleStep < meds[selectedMed].doseCount) {
            scheduleStep++; kbInput = ""; drawScheduleScreen();
          } else {
            saveMeds(); scheduleStep = 0; kbInput = "";
            currentScreen = SCREEN_MED; drawMedScreen();
          }
        } else {
          // زمان نامعتبر
          tft.fillRect(10, 35, 200, 35, BLACK);
          tft.drawRect(10, 35, 200, 35, RED);
          tft.setTextSize(2); tft.setTextColor(RED);
          tft.setCursor(15, 46); tft.print("Invalid!");
          delay(800);
          kbInput = "";
          schRefreshText();
        }
      }
    } else {
      // tx >= 225  => Cancel
      scheduleStep = 0; kbInput = "";
      currentScreen = SCREEN_MED; drawMedScreen();
    }
  }   // <-- بستن if (ty >= 180)
}     // <-- بستن خود تابع handleScheduleScreen

// ==================== ملودی بازر ====================
void resetMelody() { melStep = 0; melTimer = 0; noTone(BUZZER_PIN); }

void updateMelody() {
  unsigned long nowMs = millis();
  if (nowMs - melTimer >= (unsigned long)MEL_DUR[melStep]) {
    melTimer = nowMs;
    tone(BUZZER_PIN, MEL_NOTE[melStep], MEL_DUR[melStep]);
    melStep++;
    if (melStep >= MEL_LEN) melStep = 0;
  }
}

// ==================== صفحه‌ی آلارم ====================
#define OK_X 100
#define OK_Y 180
#define OK_W 120
#define OK_H 50

void drawAlarmScreen() {
  uint16_t c = medColors[meds[alarmMed].colorIndex];
  tft.fillScreen(BLACK);
  tft.fillRect(0, 0, 320, 55, c);
  tft.setTextColor(WHITE); tft.setTextSize(4);
  tft.setCursor(70, 12); tft.print("ALARM!");

  tft.setTextSize(3); tft.setTextColor(c);
  tft.setCursor(20, 80); tft.print(meds[alarmMed].name);

  tft.setTextSize(2); tft.setTextColor(WHITE);
  tft.setCursor(20, 125);
  tft.print("Time: ");
  tft.print(meds[alarmMed].times[alarmTimeIdx]);

  tft.fillRoundRect(OK_X, OK_Y, OK_W, OK_H, 8, GREEN);
  tft.setTextColor(BLACK); tft.setTextSize(3);
  tft.setCursor(OK_X + 38, OK_Y + 14); tft.print("OK");

  resetMelody();
}

void handleAlarm() {
  updateMelody();
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;
  if (tx > OK_X && tx < OK_X + OK_W && ty > OK_Y && ty < OK_Y + OK_H) {
    if (meds[alarmMed].takenDoses < meds[alarmMed].doseCount)
      meds[alarmMed].takenDoses++;
    saveMeds();
    noTone(BUZZER_PIN);
    resetMelody();
    alarmMed = -1; alarmTimeIdx = -1;
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
    delay(200);
  }
}

// ==================== بررسی زمان داروها ====================
void checkAlarms() {
  DateTime now = rtc.now();

  if (now.day() != lastResetDay) {
    lastResetDay = now.day();
    for (int i = 0; i < MED_COUNT; i++) meds[i].takenDoses = 0;
    if (currentScreen == SCREEN_MAIN) drawMainScreen();
  }

  if (alarmMed != -1) return;          // آلارم در جریان است
  if (now.minute() == lastTrigMin) return;

  int h = now.hour(), m = now.minute();
  for (int i = 0; i < MED_COUNT; i++) {
    if (!meds[i].active) continue;
    for (int t = 0; t < meds[i].doseCount && t < 3; t++) {
      if (parseHH(meds[i].times[t]) == h && parseMM(meds[i].times[t]) == m) {
        alarmMed = i; alarmTimeIdx = t; lastTrigMin = m;
        currentScreen = SCREEN_ALARM;
        drawAlarmScreen();
        return;
      }
    }
  }
}
// ==================== صفحه‌ی تنظیم ساعت ====================
#define TS_HX 40    // باکس ساعت
#define TS_MX 190   // باکس دقیقه
#define TS_BY 70
#define TS_BW 90
#define TS_BH 90

void tsRefreshNumbers() {
  // فقط ناحیه‌ی اعداد را پاک و دوباره رسم می‌کند (ضد چشمک)
  tft.fillRect(TS_HX + 8, TS_BY + 28, TS_BW - 16, 40, BLACK);
  tft.fillRect(TS_MX + 8, TS_BY + 28, TS_BW - 16, 40, BLACK);

  tft.setTextSize(4);
  tft.setTextColor(YELLOW);
  tft.setCursor(TS_HX + 18, TS_BY + 30);
  if (tempHH < 10) tft.print('0');
  tft.print(tempHH);

  tft.setCursor(TS_MX + 18, TS_BY + 30);
  if (tempMM < 10) tft.print('0');
  tft.print(tempMM);
}

void drawTimeSetScreen() {
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setTextColor(CYAN);
  tft.setCursor(40, 10);
  tft.print("Set System Time");

  // باکس ساعت و دقیقه
  tft.drawRoundRect(TS_HX, TS_BY, TS_BW, TS_BH, 6, WHITE);
  tft.drawRoundRect(TS_MX, TS_BY, TS_BW, TS_BH, 6, WHITE);

  // دونقطه‌ی وسط
  tft.setTextSize(4); tft.setTextColor(WHITE);
  tft.setCursor(152, TS_BY + 30); tft.print(":");

  // دکمه‌های + (بالا) و - (پایین) برای ساعت
  tft.fillTriangle(TS_HX + 45, TS_BY - 25, TS_HX + 25, TS_BY - 5,
                   TS_HX + 65, TS_BY - 5, GREEN);
  tft.fillTriangle(TS_HX + 45, TS_BY + TS_BH + 25, TS_HX + 25, TS_BY + TS_BH + 5,
                   TS_HX + 65, TS_BY + TS_BH + 5, RED);

  // دکمه‌های + و - برای دقیقه
  tft.fillTriangle(TS_MX + 45, TS_BY - 25, TS_MX + 25, TS_BY - 5,
                   TS_MX + 65, TS_BY - 5, GREEN);
  tft.fillTriangle(TS_MX + 45, TS_BY + TS_BH + 25, TS_MX + 25, TS_BY + TS_BH + 5,
                   TS_MX + 65, TS_BY + TS_BH + 5, RED);

  // دکمه‌های SAVE / CANCEL
  tft.fillRoundRect(20, 200, 130, 35, 5, GREEN);
  tft.setTextSize(2); tft.setTextColor(BLACK);
  tft.setCursor(45, 209); tft.print("SAVE");

  tft.fillRoundRect(170, 200, 130, 35, 5, RED);
  tft.setTextColor(WHITE);
  tft.setCursor(190, 209); tft.print("CANCEL");

  tsRefreshNumbers();
}

void handleTimeSetScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  // ناحیه‌ی ساعت
  if (tx > TS_HX && tx < TS_HX + TS_BW) {
    if (ty < TS_BY)              { tempHH = (tempHH + 1) % 24; tsRefreshNumbers(); delay(120); return; }
    if (ty > TS_BY + TS_BH)      { tempHH = (tempHH == 0) ? 23 : tempHH - 1; tsRefreshNumbers(); delay(120); return; }
  }
  // ناحیه‌ی دقیقه
  if (tx > TS_MX && tx < TS_MX + TS_BW) {
    if (ty < TS_BY)              { tempMM = (tempMM + 1) % 60; tsRefreshNumbers(); delay(120); return; }
    if (ty > TS_BY + TS_BH)      { tempMM = (tempMM == 0) ? 59 : tempMM - 1; tsRefreshNumbers(); delay(120); return; }
  }

  // SAVE
  if (ty > 200 && ty < 235 && tx > 20 && tx < 150) {
    DateTime now = rtc.now();
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), tempHH, tempMM, 0));
    lastClockMin = -1;          // ساعت کوچک دوباره رسم شود
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
    delay(200);
    return;
  }

  // CANCEL
  if (ty > 200 && ty < 235 && tx > 170 && tx < 300) {
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
    delay(200);
    return;
  }
}

// ==================== Setup & Loop ====================
void setup() {
  Serial.begin(9600);
  tft.reset();
  tft.begin(0x9341);
  tft.setRotation(1);

  Wire.begin();
  if (!rtc.begin()) Serial.println("RTC not found!");
  if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  loadMeds();
  lastResetDay = rtc.now().day();

  drawMainScreen();
}

void loop() {
  if (currentScreen == SCREEN_ALARM) {
    handleAlarm();
  } else {
    checkAlarms();
    switch (currentScreen) {
      case SCREEN_MAIN:     drawMiniClock(false); drawMiniDate(false); handleMainScreen();     break;
      case SCREEN_MED:      handleMedScreen();      break;
      case SCREEN_KEYBOARD: handleKeyboard();       break;
      case SCREEN_SCHEDULE: handleScheduleScreen(); break;
      case SCREEN_TIME_SET: handleTimeSetScreen();  break;
      default: break;
    }
  }
  delay(20);
}