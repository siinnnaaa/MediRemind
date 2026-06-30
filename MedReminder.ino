#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <TouchScreen.h>
#include <EEPROM.h>
#include <Wire.h>
#include <RTClib.h>

// Touchscreen hardware pin definitions
#define YP A3
#define XM A2
#define YM 9
#define XP 8

// RGB565 Color definitions
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

// Touch pressure thresholds
#define MINPRESSURE 10
#define MAXPRESSURE 1200

// Hardware Pin Configuration
#define BUZZER_PIN 53

// Verified Touchscreen Calibration Values
#define TS_MINX 181
#define TS_MAXX 938
#define TS_MINY 149
#define TS_MAXY 954

// EEPROM Storage Map
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_MAGIC_VAL  0xC4   // Change this value if the Medicine struct changes to force memory reset
#define EEPROM_MEDS_ADDR  4

// Hardware Interface Instances
MCUFRIEND_kbv tft;
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
RTC_DS3231 rtc;

// Color palette for medicine categories (RGB565 format)
const uint16_t medColors[6] = { RED, GREEN, BLUE, YELLOW, MAGENTA, ORANGE };
#define COLOR_COUNT 6

// Core Data Structure for Medicine Profiles
struct Medicine {
  bool active;            // Indicates if the slot is occupied
  char name[12];          // Medicine name string (max 11 chars + null terminator)
  uint8_t colorIndex;     // Associated theme color index
  uint8_t doseCount;      // Total scheduled doses per day (1 to 3)
  char times[3][6];       // Stored reminder times in HH:MM format
  uint8_t takenDoses;     // Tracked taken doses for the current day
};

#define MED_COUNT 5
Medicine meds[MED_COUNT];

// GUI Navigation States
enum Screen {
  SCREEN_MAIN,
  SCREEN_MED,
  SCREEN_KEYBOARD,
  SCREEN_SCHEDULE,
  SCREEN_ALARM,  
  SCREEN_TIME_SET
};

Screen currentScreen = SCREEN_MAIN;
int    selectedMed   = 0;
int tempHH = 0, tempMM = 0; // Temp variables for system clock configuration
int scheduleStep = 0;       // Keeps track of the current step during scheduling setup
int schedJY = 0, schedJM = 0, schedJD = 0;  // Stored Jalali calendar coordinates

// On-Screen Virtual Keyboard Layout Matrix
bool   kbCaps = false;
String kbInput = "";

String kbSymbol[3][10] = {
  {"Q","W","E","R","T","Y","U","I","O","P"},
  {"A","S","D","F","G","H","J","K","L",";"},
  {"Z","X","C","V","B","N","M",".","<",""}
};

// Alarm Tracking and State Registers
int    alarmMed     = -1;   // Active medication index causing the alarm trigger
int    alarmTimeIdx = -1;   // Specific dose index of the active medication
int    lastTrigMin  = -1;   // Debounce register to prevent multi-triggering within the same minute
uint8_t lastResetDay = 0;   // Register to execute daily dosage counter resets
int8_t lastClockMin = -1;   // Anti-flicker state register for the top corner mini-clock

// Non-blocking Alarm Buzzer Melody Sequence
const int MEL_NOTE[] = {659, 587, 523, 587, 659, 659, 659};
const int MEL_DUR[]  = {200, 200, 200, 200, 200, 200, 400};
#define MEL_LEN 7
uint8_t       melStep  = 0;
unsigned long melTimer = 0;

// ==================== TOUCHSCREEN SUBSYSTEM ====================
// Reads touchscreen coordinates and maps them directly to display pixels
bool getTouchXY(int &tx, int &ty) {
  TSPoint p = ts.getPoint();
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);
  if (p.z < MINPRESSURE || p.z > MAXPRESSURE) return false;
  tx = map(p.y, TS_MAXY, TS_MINY, 0, tft.width());
  ty = map(p.x, TS_MAXX, TS_MINX, 0, tft.height());
  return true;
}

// ==================== EEPROM STORAGE MANAGEMENT ====================
// Commits current data arrays down into the microcontroller's persistent EEPROM
void saveMeds() {
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
  EEPROM.put(EEPROM_MEDS_ADDR, meds);
}

// Restores medication profiles from persistent storage or formats memory on first run
void loadMeds() {
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic != EEPROM_MAGIC_VAL) {
    // Factory reset routine triggered on first boot or structural firmware updates
    for (int i = 0; i < MED_COUNT; i++) {
      memset(&meds[i], 0, sizeof(Medicine));
      strcpy(meds[i].name, "---");
      meds[i].colorIndex = i % COLOR_COUNT;
    }
    saveMeds();
    return;
  }
  
  EEPROM.get(EEPROM_MEDS_ADDR, meds);
  
  // Data sanity check and error mitigation loop
  for (int i = 0; i < MED_COUNT; i++) {
    if (meds[i].name[0] < 32 || meds[i].name[0] > 126) {
      memset(&meds[i], 0, sizeof(Medicine));
      strcpy(meds[i].name, "---");
    }
    if (meds[i].colorIndex >= COLOR_COUNT) meds[i].colorIndex = 0;
  }
}

// ==================== TIME PARSING UTILITIES ====================
int parseHH(const char* t) { return (t[0]-'0')*10 + (t[1]-'0'); }
int parseMM(const char* t) { return (t[3]-'0')*10 + (t[4]-'0'); }

// ==================== CALENDAR CONVERSION (GREGORIAN TO JALALI) ====================
// Mathematical conversion algorithm for rendering accurate local Jalali dates
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

// ==================== MAIN SCREEN INTERFACE RENDERING ====================
int8_t lastClockDay = -1;  // Anti-flicker tracker for the date element

// Renders the processed Jalali date to the top status bar area
void drawMiniDate(bool force) {
  DateTime now = rtc.now();
  if (!force && now.day() == lastClockDay) return;
  lastClockDay = now.day();

  int jy, jm, jd;
  gregorianToJalali(now.year(), now.month(), now.day(), jy, jm, jd);

  tft.fillRect(190, 8, 60, 12, BLACK);   // Clear sub-region near clock area
  tft.setTextColor(CYAN);
  tft.setTextSize(1);
  tft.setCursor(190, 8);
  tft.print(jy); tft.print('/');
  if (jm < 10) tft.print('0');
  tft.print(jm); tft.print('/');
  if (jd < 10) tft.print('0');
  tft.print(jd);
}

// Renders the dynamic systemic digital clock module
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

// Renders the overall main layout dashboard including all medication cards
void drawMainScreen() {
  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print("Med Reminder");

  for (int i = 0; i < MED_COUNT; i++) {
    int cy = 30 + i * 40;
    tft.drawRoundRect(2, cy, 316, 38, 4, CYAN);

    // Render the identifying indicator block if profile is active
    if (meds[i].active)
      tft.fillRect(300, cy + 4, 12, 12, medColors[meds[i].colorIndex]);

    tft.setTextSize(2);
    tft.setTextColor(YELLOW);
    tft.setCursor(6, cy + 4);
    tft.print(i + 1);
    tft.print(":");

    if (meds[i].active) {
      tft.print(meds[i].name);

      // Render dosage ratio text (Taken/Total) in upper right segment of card
      tft.setTextSize(1);
      tft.setTextColor(WHITE);
      tft.setCursor(210, cy + 4);
      tft.print("T:");
      tft.print(meds[i].takenDoses);
      tft.print("/");
      tft.print(meds[i].doseCount);

      // Display scheduled alarms sequence on the lower line
      tft.setTextSize(1);
      tft.setTextColor(WHITE);
      tft.setCursor(6, cy + 22);
      for (int t = 0; t < meds[i].doseCount && t < 3; t++) {
        tft.print(meds[i].times[t]);
        if (t < meds[i].doseCount - 1) tft.print(" ");
      }

      // Render graphical progress bar components tracking taken ratios
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

// Checks user input vectors on the primary menu interface
void handleMainScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  // Top-right corner bounding box interception -> Time Config Screen
  if (tx > 250 && ty < 25) {
    DateTime now = rtc.now();
    tempHH = now.hour();
    tempMM = now.minute();
    currentScreen = SCREEN_TIME_SET;
    drawTimeSetScreen();
    return;
  }

  // Iterate row card collision zones to catch selection actions
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

// ==================== COLOR SELECTOR WIDGET ====================
#define SW_Y    165
#define SW_SIZE 45
#define SW_GAP  5
#define SW_X0   10

// Draws the horizontal layout of selectable color nodes
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

// Maps input coordinate grids back to the corresponding color array index
int hitColorPicker(int x, int y) {
  if (y < SW_Y || y > SW_Y + SW_SIZE) return -1;
  for (uint8_t i = 0; i < COLOR_COUNT; i++) {
    int sx = SW_X0 + i * (SW_SIZE + SW_GAP);
    if (x > sx && x < sx + SW_SIZE) return i;
  }
  return -1;
}

// ==================== MEDICATION INTERFACE MANAGMENT ====================
// Generates the control center menu layout for a chosen item row
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

// Directs functional touch logic mapping for the specific medicine settings panel
void handleMedScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  // Intercept operations targeted at color picking zones
  int ci = hitColorPicker(tx, ty);
  if (ci != -1) {
    meds[selectedMed].colorIndex = ci;
    saveMeds();
    drawColorPicker(ci);
    delay(150);
    return;
  }

  if (tx > 10 && tx < 150 && ty > 40 && ty < 85) {
    // "Set Name" processing initiation
    kbInput = "";
    kbCaps  = true;
    currentScreen = SCREEN_KEYBOARD;
    drawKeyboard();
  } else if (tx > 170 && tx < 310 && ty > 40 && ty < 85) {
    // "Schedule" setup initialization
    DateTime now = rtc.now();
    gregorianToJalali(now.year(), now.month(), now.day(), schedJY, schedJM, schedJD);
    currentScreen = SCREEN_SCHEDULE;
    drawScheduleScreen();
  } else if (tx > 10 && tx < 150 && ty > 100 && ty < 145) {
    // Profile erasure execution block
    memset(&meds[selectedMed], 0, sizeof(Medicine));
    strcpy(meds[selectedMed].name, "---");
    saveMeds();
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
  } else if (tx > 170 && tx < 310 && ty > 100 && ty < 145) {
    // Standard back escape route processing
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
  }
}

// ==================== VIRTUAL KEYBOARD SUBSYSTEM ====================
// Builds graphic presentation map layouts for typing interfaces
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

// Localized update mechanism providing isolated text preview box refreshes
void kbRefreshText() {
  tft.fillRect(0, 0, 320, 45, BLACK);
  tft.drawRect(0, 0, 320, 45, WHITE);
  tft.setTextSize(2); tft.setTextColor(WHITE);
  tft.setCursor(4, 12); tft.print(kbInput);
}

// Interprets incoming coordinates map inputs directly across alpha key matrices
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

  // Intercept Spacebar element coordinates
  if (ty >= 170 && ty < 205 && tx < 193) {
    if (kbInput.length() < 11) { kbInput += " "; kbRefreshText(); }
    return;
  }
  // Intercept Backspace element coordinates
  if (ty >= 170 && ty < 205 && tx >= 193) {
    if (kbInput.length() > 0) { kbInput.remove(kbInput.length()-1); kbRefreshText(); }
    return;
  }

  // Handle systemic operations processing row (Caps, OK, Cancel)
  if (ty >= 208) {
    if (tx < 98) { kbCaps = !kbCaps; drawKeyboard(); return; } // Shift character mappings state
    if (tx < 195) { // OK operation branch execution
      if (kbInput.length() > 0) {
        kbInput.toCharArray(meds[selectedMed].name, 12);
        meds[selectedMed].active = true;
        saveMeds();
      }
      currentScreen = SCREEN_MED;
      drawMedScreen();
      return;
    }
    // Cancel operation handling fallback
    currentScreen = SCREEN_MED;
    drawMedScreen();
    return;
  }
}

// ==================== SCHEDULE PROFILER SCREEN ====================
// Renders user flow control elements related to intake configuration metrics
void drawScheduleScreen() {
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);

  if (scheduleStep == 0) {
    // Step 0: Define total daily dosage quantity benchmarks
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
    tft.setTextSize(2);   // Reset text scale bounds back up

    for (int i = 1; i <= 3; i++) {
      tft.fillRoundRect(20 + (i-1)*100, 60, 80, 60, 6, BLUE);
      tft.setTextSize(3); tft.setTextColor(WHITE);
      tft.setCursor(50 + (i-1)*100, 78); tft.print(i);
    }
    tft.fillRoundRect(100, 170, 120, 40, 5, GREY);
    tft.setTextSize(2); tft.setTextColor(WHITE);
    tft.setCursor(125, 183); tft.print("< Back");
  } else {
    // Step > 0: Assign execution times (HH:MM) to each numbered dosage instance
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

// Contextually manages local input buffer string modifications
void schRefreshText() {
  tft.fillRect(10, 35, 200, 35, BLACK);
  tft.drawRect(10, 35, 200, 35, WHITE);
  tft.setTextSize(2); tft.setTextColor(YELLOW);
  tft.setCursor(15, 46); tft.print(kbInput);
}

// Parses matrix selections across scheduling initialization wizards
void handleScheduleScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  if (scheduleStep == 0) {
    // Intercept quantity selection matrices
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
    // Capture cancel-back escape triggers
    if (ty > 170 && ty < 210 && tx > 100 && tx < 220) {
      scheduleStep = 0;
      currentScreen = SCREEN_MED;
      drawMedScreen();
    }
    return;
  }

  // Route numeric input keys bounding configurations
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

  // Bottom contextual row control block processing zone
  if (ty >= 180) {
    if (tx < 60) {
      // Append delimiter char block loop
      if (kbInput.length() < 5 && kbInput.indexOf(':') < 0) { kbInput += ":"; schRefreshText(); }
    } else if (tx < 155) {
      // Execute inner variable clear action
      if (kbInput.length() > 0) { kbInput.remove(kbInput.length()-1); schRefreshText(); }
    } else if (tx < 225) {
      // Validate chronological integrity properties
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
          // Render error layout if chronological attributes bounds fail validation
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
      // tx >= 225 => Handle configuration termination escape
      scheduleStep = 0; kbInput = "";
      currentScreen = SCREEN_MED; drawMedScreen();
    }
  } // End of if (ty >= 180)
} // End of handleScheduleScreen

// ==================== AUDIO ANNOUNCEMENT SUBSYSTEM ====================
void resetMelody() { melStep = 0; melTimer = 0; noTone(BUZZER_PIN); }

// Drives synchronous step indexing across tone frequencies asynchronously
void updateMelody() {
  unsigned long nowMs = millis();
  if (nowMs - melTimer >= (unsigned long)MEL_DUR[melStep]) {
    melTimer = nowMs;
    tone(BUZZER_PIN, MEL_NOTE[melStep], MEL_DUR[melStep]);
    melStep++;
    if (melStep >= MEL_LEN) melStep = 0;
  }
}

// ==================== HIGH-PRIORITY ALARM INTERFACE ====================
#define OK_X 100
#define OK_Y 180
#define OK_W 120
#define OK_H 50

// Overwrites display map grids with full-frame emergency alert designs
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

// Processes interactive user responses confirming medication intake
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

// ==================== SYSTEM TIME VALIDATION ENGINES ====================
// Periodic evaluation engine scanning for cross-matching active alerts criteria
void checkAlarms() {
  DateTime now = rtc.now();

  // Evaluates internal system state to prompt comprehensive daily metric cycle updates
  if (now.day() != lastResetDay) {
    lastResetDay = now.day();
    for (int i = 0; i < MED_COUNT; i++) meds[i].takenDoses = 0;
    if (currentScreen == SCREEN_MAIN) drawMainScreen();
  }

  if (alarmMed != -1) return;          // Bypass evaluations if a profile alert remains unhandled
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

// ==================== CHRONOLOGICAL SYSTEM CONFIG PANEL ====================
#define TS_HX 40    // Hour component bounding box X position
#define TS_MX 190   // Minute component bounding box X position
#define TS_BY 70
#define TS_BW 90
#define TS_BH 90

// Updates numeric digits with isolated graphic redraw passes to circumvent screen strobe artifacts
void tsRefreshNumbers() {
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

// Constructs complete graphical canvas mapping parameters for RTC calibrations
void drawTimeSetScreen() {
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setTextColor(CYAN);
  tft.setCursor(40, 10);
  tft.print("Set System Time");

  // Draw outlines for Hour and Minute display boxes
  tft.drawRoundRect(TS_HX, TS_BY, TS_BW, TS_BH, 6, WHITE);
  tft.drawRoundRect(TS_MX, TS_BY, TS_BW, TS_BH, 6, WHITE);

  // Center flashing colon placeholder separator segment
  tft.setTextSize(4); tft.setTextColor(WHITE);
  tft.setCursor(152, TS_BY + 30); tft.print(":");

  // Draw hour modification increment (+) and decrement (-) directional arrows
  tft.fillTriangle(TS_HX + 45, TS_BY - 25, TS_HX + 25, TS_BY - 5,
                   TS_HX + 65, TS_BY - 5, GREEN);
  tft.fillTriangle(TS_HX + 45, TS_BY + TS_BH + 25, TS_HX + 25, TS_BY + TS_BH + 5,
                   TS_HX + 65, TS_BY + TS_BH + 5, RED);

  // Draw minute modification increment (+) and decrement (-) directional arrows
  tft.fillTriangle(TS_MX + 45, TS_BY - 25, TS_MX + 25, TS_BY - 5,
                   TS_MX + 65, TS_BY - 5, GREEN);
  tft.fillTriangle(TS_MX + 45, TS_BY + TS_BH + 25, TS_MX + 25, TS_BY + TS_BH + 5,
                   TS_MX + 65, TS_BY + TS_BH + 5, RED);

  // Action execution links (SAVE / CANCEL control widgets)
  tft.fillRoundRect(20, 200, 130, 35, 5, GREEN);
  tft.setTextSize(2); tft.setTextColor(BLACK);
  tft.setCursor(45, 209); tft.print("SAVE");

  tft.fillRoundRect(170, 200, 130, 35, 5, RED);
  tft.setTextColor(WHITE);
  tft.setCursor(190, 209); tft.print("CANCEL");

  tsRefreshNumbers();
}

// Resolves interactions processed within hardware configuration menus
void handleTimeSetScreen() {
  int tx, ty;
  if (!getTouchXY(tx, ty)) return;

  // Intercept Hour configuration control quadrants
  if (tx > TS_HX && tx < TS_HX + TS_BW) {
    if (ty < TS_BY)              { tempHH = (tempHH + 1) % 24; tsRefreshNumbers(); delay(120); return; }
    if (ty > TS_BY + TS_BH)      { tempHH = (tempHH == 0) ? 23 : tempHH - 1; tsRefreshNumbers(); delay(120); return; }
  }
  // Intercept Minute configuration control quadrants
  if (tx > TS_MX && tx < TS_MX + TS_BW) {
    if (ty < TS_BY)              { tempMM = (tempMM + 1) % 60; tsRefreshNumbers(); delay(120); return; }
    if (ty > TS_BY + TS_BH)      { tempMM = (tempMM == 0) ? 59 : tempMM - 1; tsRefreshNumbers(); delay(120); return; }
  }

  // SAVE logic: Commit modified timestamps directly down to the hardware RTC registry map
  if (ty > 200 && ty < 235 && tx > 20 && tx < 150) {
    DateTime now = rtc.now();
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), tempHH, tempMM, 0));
    lastClockMin = -1;          // Flag resetting to clear mini clock cache indices
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
    delay(200);
    return;
  }

  // CANCEL logic: Revert system display layout state back into standard parameters
  if (ty > 200 && ty < 235 && tx > 170 && tx < 300) {
    currentScreen = SCREEN_MAIN;
    drawMainScreen();
    delay(200);
    return;
  }
}

// ==================== CORE FIRMWARE INITIALIZATION ====================
void setup() {
  Serial.begin(9600);
  tft.reset();
  tft.begin(0x9341); // Initialize standard ILI9341 display driver register addresses
  tft.setRotation(1); // Set system environment rendering layout perspective to Landscape mode

  Wire.begin();
  if (!rtc.begin()) Serial.println("RTC not found!");
  
  // Power loss fallback behavior validation check
  if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  loadMeds(); // Recover profile properties states out of data structures
  lastResetDay = rtc.now().day();

  drawMainScreen();
}

// ==================== MAIN EXECUTION LOOP ====================
void loop() {
  if (currentScreen == SCREEN_ALARM) {
    handleAlarm(); // Route execution pathways to intercept high-priority alerts
  } else {
    checkAlarms(); // Continuously parse scheduling attributes
    switch (currentScreen) {
      case SCREEN_MAIN:     
        drawMiniClock(false); 
        drawMiniDate(false); 
        handleMainScreen();    
        break;
      case SCREEN_MED:       
        handleMedScreen();      
        break;
      case SCREEN_KEYBOARD: 
        handleKeyboard();       
        break;
      case SCREEN_SCHEDULE: 
        handleScheduleScreen(); 
        break;
      case SCREEN_TIME_SET: 
        handleTimeSetScreen();  
        break;
      default: 
        break;
    }
  }
  delay(20); // Minor task-scheduling duty delay to optimize MCU loop iterations
}
