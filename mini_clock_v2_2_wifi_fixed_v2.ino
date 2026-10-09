#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define TOUCH_PIN    2
#define I2C_SDA      8
#define I2C_SCL      9

#define FIRMWARE_VERSION "v2.2.1"
#define AUTHOR_NAME      "Ng Huy"
#define WIFI_SSID        "huy"
#define WIFI_PASS        "huy122011"
#define AP_SSID          "By Ng Huy"
#define AP_PASS          "Huy291211"

#define LONG_PRESS_MS        650UL
#define WIFI_ATTEMPT_TIMEOUT 180000UL

#define INPUT_SOURCE 1

#if INPUT_SOURCE == 0
  #define INPUT_ACTIVE   HIGH
  #define INPUT_PIN_MODE INPUT
#else
  #define INPUT_ACTIVE   LOW
  #define INPUT_PIN_MODE INPUT_PULLUP
#endif

#define WIFI_TX_POWER WIFI_POWER_8_5dBm

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

long timezoneOffset = 7 * 3600;
long manualOffset   = 0;

enum State {
  ST_CLOCK, ST_MENU, ST_GAMES_MENU,
  ST_SET_TIME, ST_SETTINGS, ST_ABOUT,
  ST_GAME_NUM, ST_GAME_FLAPPY, ST_GAME_STACK, ST_GAME_REACTION
};
State currentState = ST_CLOCK;

bool screenOn = true;
unsigned long screenOffTimer = 0;
unsigned long screenTimeout = 15000;
const unsigned long TIMEOUT_OPTIONS[] = {5000, 10000, 15000, 30000, 60000, 0};
const int TIMEOUT_COUNT = 6;
int timeoutIdx = 2;
bool is24Hour = true;

bool blinkState = true;
unsigned long lastBlinkMs = 0;

const int MENU_COUNT = 5;
const char* menuItems[MENU_COUNT] = {"Cai dat gio", "Tro choi", "Cai dat", "Thong tin", "Quay lai"};
int menuIdx = 0;

const int GMENU_COUNT = 5;
const char* gmenuItems[GMENU_COUNT] = {"So 5", "Flappy Bird", "Stack", "Phan xa", "Quay lai"};
int gmenuIdx = 0;

int setHour = 0, setMin = 0, setField = 0;

int numState = 0;
int numVal = 1, numScore = 0, numBest = 0;
unsigned long numTick = 0;

#define BIRD_X 20
#define BIRD_W 8
#define BIRD_H 6
#define PIPE_W 12
#define PIPE_GAP 28
#define GROUND_H 6
float birdY = 20, birdV = 0;
int pipeX = 128, pipeGapY = 20;
int flappyState = 0;
int flappyScore = 0, flappyBest = 0;
unsigned long flappyLastTick = 0;

int stackState = 0;
int stackScore = 0, stackBest = 0;
int stackBlockX = 20, stackBlockY = 52;
int stackBlockW = 44;
int stackDir = 1;
int stackSpeed = 2;
int stackPlacedX = 42, stackPlacedW = 44;

int reactionState = 0;
int reactionScore = 0, reactionBest = 0;
unsigned long reactionStartMs = 0;
unsigned long reactionWaitMs = 0;

bool wifiEnabled = true;
unsigned long wifiAttemptTimer = 0;

const int SETTINGS_COUNT = 4;
const char* settingsItems[SETTINGS_COUNT] = {"12/24h", "Timeout", "WiFi", "Quay lai"};
int settingsIdx = 0;

bool touchNow = false, touchPrev = false;
unsigned long touchStartMs = 0;
bool longFired = false, justWoke = false;

time_t getLocalEpoch() {
  return (time_t)(timeClient.getEpochTime() + timezoneOffset + manualOffset);
}

inline bool readInput() {
  return digitalRead(TOUCH_PIN) == INPUT_ACTIVE;
}

void updateBlink() {
  if (millis() - lastBlinkMs > 400) {
    blinkState = !blinkState;
    lastBlinkMs = millis();
  }
}

void transitionEffect() {
  // Android TV-style quick slide transition, kept short for responsive input.
  for (int x = 0; x <= 128; x += 16) {
    display.clearDisplay();
    display.drawRoundRect(4 + (x / 4), 5, 120 - (x / 2), 54, 7, SSD1306_WHITE);
    display.display();
    delay(8);
  }
  display.clearDisplay();
  display.display();
}

void showLoading(const char* label, unsigned long durationMs) {

  if (durationMs < 2000) durationMs = 2000;
  unsigned long start = millis();
  int frame = 0;
  while (millis() - start < durationMs) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 4);
    display.print(label);

    const int cx = 64, cy = 34, r = 12;
    for (int i = 0; i < 8; i++) {
      int idx = (i + frame) % 8;
      float a = idx * 0.785398f;
      int x = cx + (int)(cos(a) * r);
      int y = cy + (int)(sin(a) * r);
      if (i == 0) display.fillCircle(x, y, 3, SSD1306_WHITE);
      else display.fillCircle(x, y, 1, SSD1306_WHITE);
    }
    int elapsed = millis() - start;
    int progressW = (elapsed * 80) / durationMs;
    if (progressW > 80) progressW = 80;
    display.drawRoundRect(24, 53, 80, 6, 2, SSD1306_WHITE);
    display.fillRoundRect(24, 53, progressW, 6, 2, SSD1306_WHITE);
    display.display();
    frame = (frame + 1) % 8;
    delay(70);
  }
}

void cursorFlash() {
  for (int i = 0; i < 2; i++) {
    blinkState = false; render(); delay(60);
    blinkState = true;  render(); delay(60);
  }
}

void goToState(State s) {
  if (s != currentState) {
    transitionEffect();
    currentState = s;
  }
}

void drawBootScreen() {
  display.clearDisplay();
  display.drawRoundRect(0, 0, 128, 64, 6, SSD1306_WHITE);
  display.drawRoundRect(2, 2, 124, 60, 5, SSD1306_WHITE);

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  const char* title = "MINI  CLOCK";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 8);
  display.print(title);

  display.getTextBounds(FIRMWARE_VERSION, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 19);
  display.print(FIRMWARE_VERSION);

  display.drawLine(18, 30, 110, 30, SSD1306_WHITE);

  const char* lbl = "TAC GIA";
  display.getTextBounds(lbl, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 34);
  display.print(lbl);

  display.getTextBounds(AUTHOR_NAME, 0, 0, &x1, &y1, &w, &h);
  int ax = (128 - w) / 2;
  int ay = 46;
  display.drawRect(ax - 4, ay - 2, w + 8, h + 4, SSD1306_WHITE);
  display.setCursor(ax, ay);
  display.print(AUTHOR_NAME);

  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT_PIN_MODE);
  randomSeed(analogRead(0));

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 fail");
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.persistent(false);

  drawBootScreen();
  delay(5000);
  showLoading("Dang khoi dong...", 2000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Dang ket noi WiFi...");
  display.print("SSID: "); display.println(WIFI_SSID);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000UL) {
    delay(200);
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Dang ket noi WiFi...");
    display.print(WIFI_SSID);
    display.print(" ");
    int dots = (int)((millis() - wifiStart) / 300) % 4;
    for (int i = 0; i < dots; i++) display.print(".");
    display.display();
  }

  if (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Khong ket noi duoc");
    display.println();
    display.println("Mo WiFi cau hinh:");
    display.print(AP_SSID);
    display.display();
    delay(700);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    if (!wm.autoConnect(AP_SSID, AP_PASS)) {
      ESP.restart();
    }
  }

  showLoading("Dong bo thoi gian...", 1500);
  timeClient.begin();
  timeClient.update();

  wifiEnabled = true;
  wifiAttemptTimer = millis();
  currentState = ST_CLOCK;
  screenOffTimer = millis();
}

void loop() {
  updateBlink();
  timeClient.update();
  handleTouch();
  wifiTimeoutCheck();

  if (currentState == ST_GAME_NUM && numState == 1) {
    if (millis() - numTick > 500) {
      numVal = random(1, 10);
      numTick = millis();
    }
  }
  else if (currentState == ST_GAME_FLAPPY && flappyState == 1) {
    if (millis() - flappyLastTick > 40) {
      flappyTick();
      flappyLastTick = millis();
    }
  }
  else if (currentState == ST_GAME_STACK && stackState == 1) {
    stackTick();
  }
  else if (currentState == ST_GAME_REACTION && reactionState == 1) {
    reactionTick();
  }

  if (screenOn) {
    render();
    if (currentState == ST_CLOCK && screenTimeout > 0 &&
        (millis() - screenOffTimer > screenTimeout)) {
      screenOn = false;
      display.ssd1306_command(SSD1306_DISPLAYOFF);
    }
  }
  delay(20);
}

void wifiTimeoutCheck() {
  if (!wifiEnabled) return;
  if (WiFi.getMode() == WIFI_OFF) return;

  if (WiFi.status() == WL_CONNECTED || WiFi.softAPgetStationNum() > 0) {
    wifiAttemptTimer = millis();
  } else if (millis() - wifiAttemptTimer > WIFI_ATTEMPT_TIMEOUT) {
    Serial.println("WiFi timeout - tat de tiet kiem pin");
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;
  }
}

void handleTouch() {
  touchPrev = touchNow;
  touchNow = readInput();

  if (touchNow && !touchPrev) {
    touchStartMs = millis();
    longFired = false;

    if (!screenOn) {
      screenOn = true;
      display.ssd1306_command(SSD1306_DISPLAYON);
      justWoke = true;
      currentState = ST_CLOCK;
    }
    screenOffTimer = millis();
  }

  if (touchNow && !longFired && (millis() - touchStartMs >= LONG_PRESS_MS)) {
    longFired = true;
    if (!justWoke) onLongPress();
  }

  if (!touchNow && touchPrev) {
    if (!longFired && !justWoke) onShortPress();
    justWoke = false;
  }
}

void onShortPress() {
  screenOffTimer = millis();

  switch (currentState) {
    case ST_CLOCK: break;

    case ST_MENU:
      menuIdx = (menuIdx + 1) % MENU_COUNT;
      cursorFlash();
      break;

    case ST_GAMES_MENU:
      gmenuIdx = (gmenuIdx + 1) % GMENU_COUNT;
      cursorFlash();
      break;

    case ST_SET_TIME:
      if (setField == 0)      setHour = (setHour + 1) % 24;
      else if (setField == 1) setMin  = (setMin + 1) % 60;
      break;

    case ST_SETTINGS:
      settingsIdx = (settingsIdx + 1) % SETTINGS_COUNT;
      cursorFlash();
      break;

    case ST_GAME_NUM:
      if (numState == 0) {
        numState = 1; numScore = 0;
        numVal = random(1, 10); numTick = millis();
      } else if (numState == 1) {
        if (numVal == 5) numScore++;
        else {
          numState = 2;
          if (numScore > numBest) numBest = numScore;
        }
        numVal = random(1, 10); numTick = millis();
      } else {
        numState = 1; numScore = 0;
        numVal = random(1, 10); numTick = millis();
      }
      break;

    case ST_GAME_STACK:
      if (stackState == 0) {
        stackState = 1;
      } else if (stackState == 1) {
        stackPlace();
      } else {
        stackInit(); stackState = 1;
      }
      break;

    case ST_GAME_REACTION:
      if (reactionState == 0 || reactionState == 3) {
        reactionInit();
      } else {
        reactionPress();
      }
      break;

    case ST_GAME_FLAPPY:
      if (flappyState == 0) {
        flappyState = 1; flappyLastTick = millis();
      } else if (flappyState == 1) {
        birdV = -3.2f;
      } else {
        flappyInit(); flappyState = 1; flappyLastTick = millis();
      }
      break;

    case ST_ABOUT: break;
  }
}

void onLongPress() {
  screenOffTimer = millis();

  switch (currentState) {
    case ST_CLOCK:
      goToState(ST_MENU); menuIdx = 0;
      break;

    case ST_MENU:
      switch (menuIdx) {
        case 0:
          setHour = (getLocalEpoch() / 3600) % 24;
          setMin  = (getLocalEpoch() / 60) % 60;
          setField = 0;
          goToState(ST_SET_TIME);
          break;
        case 1: goToState(ST_GAMES_MENU); gmenuIdx = 0; break;
        case 2: settingsIdx = 0; goToState(ST_SETTINGS); break;
        case 3: goToState(ST_ABOUT); break;
        case 4: goToState(ST_CLOCK); break;
      }
      break;

    case ST_GAMES_MENU:
      if (gmenuIdx == 0)      { numState = 0; goToState(ST_GAME_NUM); }
      else if (gmenuIdx == 1) { flappyInit(); flappyState = 0; goToState(ST_GAME_FLAPPY); }
      else if (gmenuIdx == 2) { stackInit(); stackState = 0; goToState(ST_GAME_STACK); }
      else if (gmenuIdx == 3) { reactionState = 0; goToState(ST_GAME_REACTION); }
      else                    { goToState(ST_MENU); menuIdx = 1; }
      break;

    case ST_SET_TIME:
      if (setField == 0) {
        setField = 1;
      } else {
        time_t cur = getLocalEpoch();
        time_t dayStart = (cur / 86400) * 86400;
        time_t target = dayStart + setHour * 3600 + setMin * 60;
        manualOffset += (target - cur);
        goToState(ST_CLOCK);
      }
      break;

    case ST_SETTINGS:
      switch (settingsIdx) {
        case 0: is24Hour = !is24Hour; break;
        case 1:
          timeoutIdx = (timeoutIdx + 1) % TIMEOUT_COUNT;
          screenTimeout = TIMEOUT_OPTIONS[timeoutIdx];
          break;
        case 2: toggleWiFi(); break;
        case 3: goToState(ST_MENU); menuIdx = 2; break;
      }
      break;

    case ST_GAME_NUM:
    case ST_GAME_FLAPPY:
    case ST_GAME_STACK:
    case ST_GAME_REACTION:
      goToState(ST_GAMES_MENU);
      break;

    case ST_ABOUT:
      goToState(ST_MENU); menuIdx = 3;
      break;
  }
}

void toggleWiFi() {
  if (wifiEnabled) {

    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(20, 26);
    display.print("Da TAT WiFi");
    display.display();
    delay(800);
  } else {

    wifiEnabled = true;
    wifiAttemptTimer = millis();

    showLoading("Dang bat WiFi...", 1200);
    WiFi.mode(WIFI_STA);
    WiFi.begin();

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 5000) {
      delay(200);
      display.clearDisplay();
      display.setCursor(0, 20);
      display.print("Dang ket noi");
      for (int i = 0; i < (int)((millis() - t0) / 300) % 4; i++) display.print(".");
      display.display();
    }

    if (WiFi.status() == WL_CONNECTED) {
      display.clearDisplay();
      display.setCursor(20, 26);
      display.print("WiFi da ket noi");
      display.display();
      delay(700);
    } else {
      display.clearDisplay();
      display.setCursor(10, 20);
      display.print("Khong ket noi duoc");
      display.setCursor(10, 34);
      display.print("Se tat sau 3 phut");
      display.display();
      delay(1200);
    }
  }
}

void enterWiFiConfig() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("== CAU HINH WiFi ==");
  display.println();
  display.println("WiFi tu dong:");
  display.print("   "); display.println(WIFI_SSID);
  display.println();
  display.println("AP cau hinh:");
  display.print("   "); display.println(AP_SSID);
  display.print("Pass: "); display.println(AP_PASS);
  display.println("IP: 192.168.4.1");
  display.display();
  delay(2500);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  wm.startConfigPortal(AP_SSID, AP_PASS);

  showLoading("Dang cap nhat...", 1200);
  wifiEnabled = true;
  wifiAttemptTimer = millis();
  goToState(ST_CLOCK);
  screenOffTimer = millis();
}

void flappyInit() {
  birdY = 20; birdV = 0;
  pipeX = 128;
  pipeGapY = 6 + random(0, 25);
  flappyScore = 0;
}

void flappyTick() {
  birdV += 0.65f;
  if (birdV > 3.0f) birdV = 3.0f;
  birdY += birdV;

  pipeX -= 2;
  if (pipeX < -PIPE_W) {
    pipeX = 128;
    pipeGapY = 6 + random(0, 25);
    flappyScore++;
    if (flappyScore > flappyBest) flappyBest = flappyScore;
  }

  if (birdY + BIRD_H > 64 - GROUND_H) {
    birdY = 64 - GROUND_H - BIRD_H;
    flappyState = 2;
  }
  if (birdY < 0) { birdY = 0; birdV = 0; }

  if (BIRD_X + BIRD_W > pipeX && BIRD_X < pipeX + PIPE_W) {
    if (birdY < pipeGapY || birdY + BIRD_H > pipeGapY + PIPE_GAP) {
      flappyState = 2;
    }
  }
}

void renderFlappy() {
  display.clearDisplay();
  if (flappyState == 2) {
    display.setTextSize(1);
    display.setCursor(30, 10); display.print("GAME OVER");
    display.setCursor(30, 26); display.print("Diem: "); display.print(flappyScore);
    display.setCursor(30, 36); display.print("Best: "); display.print(flappyBest);
    display.setCursor(20, 50); display.print("Ngan: Choi lai");
    return;
  }
  if (flappyState == 0) {
    display.setTextSize(1);
    display.setCursor(24, 20); display.print("FLAPPY BIRD");
    display.setCursor(12, 36); display.print("Ngan: Vo canh");
    display.setCursor(12, 48); display.print("Giu : Thoat");
    return;
  }

  display.fillRect(0, 64 - GROUND_H, 128, GROUND_H, SSD1306_WHITE);
  display.fillRect(pipeX, 0, PIPE_W, pipeGapY, SSD1306_WHITE);
  int botY = pipeGapY + PIPE_GAP;
  if (botY < 64 - GROUND_H) {
    display.fillRect(pipeX, botY, PIPE_W, 64 - GROUND_H - botY, SSD1306_WHITE);
  }
  display.fillRect(BIRD_X, (int)birdY, BIRD_W, BIRD_H, SSD1306_WHITE);
  display.drawPixel(BIRD_X + BIRD_W - 2, (int)birdY + 2, SSD1306_BLACK);

  display.setTextSize(1);
  display.setCursor(60, 2);
  display.print(flappyScore);
}

void stackInit() {
  stackState = 0;
  stackScore = 0;
  stackBlockW = 44;
  stackBlockX = 20;
  stackBlockY = 52;
  stackPlacedX = 42;
  stackPlacedW = 44;
  stackDir = 1;
  stackSpeed = 2;
}

void stackTick() {
  stackBlockX += stackDir * stackSpeed;
  if (stackBlockX <= 0) { stackBlockX = 0; stackDir = 1; }
  if (stackBlockX + stackBlockW >= 128) { stackBlockX = 128 - stackBlockW; stackDir = -1; }
  if (stackScore > 8) stackSpeed = 3;
  if (stackScore > 16) stackSpeed = 4;
}

void stackPlace() {
  int left = max(stackBlockX, stackPlacedX);
  int right = min(stackBlockX + stackBlockW, stackPlacedX + stackPlacedW);
  int overlap = right - left;
  if (overlap <= 0) {
    stackState = 2;
    if (stackScore > stackBest) stackBest = stackScore;
    return;
  }
  stackPlacedX = left;
  stackPlacedW = overlap;
  stackScore++;
  stackBlockW = stackPlacedW;
  stackBlockY -= 7;
  if (stackBlockY < 10) {
    if (stackScore > stackBest) stackBest = stackScore;
    stackState = 2;
  }
}

void renderStack() {
  display.clearDisplay();
  display.setTextSize(1);
  if (stackState == 0) {
    display.setCursor(30, 8); display.print("STACK");
    display.setCursor(10, 25); display.print("Canh khoi chay");
    display.setCursor(10, 38); display.print("Cham de dat khoi");
    display.setCursor(10, 52); display.print("Giu: Thoat");
    return;
  }
  if (stackState == 2) {
    display.setCursor(32, 10); display.print("GAME OVER");
    display.setCursor(30, 26); display.print("Diem: "); display.print(stackScore);
    display.setCursor(30, 38); display.print("Best: "); display.print(stackBest);
    display.setCursor(18, 52); display.print("Cham: Choi lai");
    return;
  }
  display.drawRect(0, 59, 128, 5, SSD1306_WHITE);
  display.fillRect(stackPlacedX, stackBlockY + 7, stackPlacedW, 7, SSD1306_WHITE);
  display.fillRect(stackBlockX, stackBlockY, stackBlockW, 7, SSD1306_WHITE);
  display.setCursor(4, 2); display.print("Diem: "); display.print(stackScore);
}

void reactionInit() {
  reactionScore = 0;
  reactionState = 1;
  reactionWaitMs = random(1200, 3000);
  reactionStartMs = millis();
}

void reactionTick() {

  if (millis() - reactionStartMs >= reactionWaitMs) {
    reactionState = 2;
  }
}

void reactionPress() {
  if (reactionState == 1) {
    reactionState = 3;
    if (reactionScore > reactionBest) reactionBest = reactionScore;
  } else if (reactionState == 2) {
    reactionScore++;
    if (reactionScore > reactionBest) reactionBest = reactionScore;
    reactionWaitMs = random(900, 2500);
    reactionStartMs = millis();
    reactionState = 1;
  } else {
    reactionInit();
  }
}

void renderReaction() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(34, 4); display.print("PHAN XA");
  if (reactionState == 0) {
    display.setCursor(12, 25); display.print("Cho GO roi cham!");
    display.setCursor(28, 43); display.print("Cham: Bat dau");
  } else if (reactionState == 1) {
    display.setTextSize(2);
    display.setCursor(38, 22); display.print("CHO...");
    display.setTextSize(1);
    display.setCursor(35, 50); display.print("Diem: "); display.print(reactionScore);
  } else if (reactionState == 2) {
    display.setTextSize(2);
    display.setCursor(42, 20); display.print("GO!");
    display.setTextSize(1);
    display.setCursor(35, 50); display.print("Diem: "); display.print(reactionScore);
  } else {
    display.setCursor(22, 12); display.print("QUA SOM!");
    display.setCursor(32, 28); display.print("Diem: "); display.print(reactionScore);
    display.setCursor(32, 40); display.print("Best: "); display.print(reactionBest);
    display.setCursor(18, 54); display.print("Cham: Choi lai");
  }
}

void render() {
  display.clearDisplay();
  switch (currentState) {
    case ST_CLOCK:      renderClock();     break;
    case ST_MENU:       renderMenu();      break;
    case ST_GAMES_MENU: renderGamesMenu(); break;
    case ST_SET_TIME:   renderSetTime();   break;
    case ST_SETTINGS:   renderSettings();  break;
    case ST_ABOUT:      renderAbout();     break;
    case ST_GAME_NUM:    renderNumGame();   break;
    case ST_GAME_FLAPPY: renderFlappy();    break;
    case ST_GAME_STACK:  renderStack();     break;
    case ST_GAME_REACTION: renderReaction(); break;
  }
  display.display();
}

void renderClock() {
  time_t e = getLocalEpoch();
  int h = (e / 3600) % 24;
  int m = (e / 60) % 60;
  int sec = e % 60;

  struct tm *t = gmtime(&e);
  const char* days[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

  char dateBuf[20];
  sprintf(dateBuf, "%s  %02d/%02d/%02d",
          days[t->tm_wday], t->tm_mday, t->tm_mon + 1, (t->tm_year + 1900) % 100);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(3, 2);
  display.print(dateBuf);

  bool wifiOK = wifiEnabled && WiFi.status() == WL_CONNECTED;
  int wx = 116, wy = 7;
  if (wifiOK) {
    if (blinkState) {
      display.drawCircle(wx, wy + 3, 7, SSD1306_WHITE);
      display.drawCircle(wx, wy + 3, 4, SSD1306_WHITE);
      display.fillCircle(wx, wy + 3, 1, SSD1306_WHITE);
      display.fillRect(wx - 8, wy - 4, 17, 7, SSD1306_BLACK);
      display.drawLine(wx - 6, wy + 1, wx, wy - 4, SSD1306_WHITE);
      display.drawLine(wx, wy - 4, wx + 6, wy + 1, SSD1306_WHITE);
      display.drawLine(wx - 4, wy + 3, wx, wy, SSD1306_WHITE);
      display.drawLine(wx, wy, wx + 4, wy + 3, SSD1306_WHITE);
    } else {
      display.fillCircle(wx, wy + 3, 2, SSD1306_WHITE);
    }
  } else {
    display.drawCircle(wx, wy + 3, 2, SSD1306_WHITE);
    display.drawLine(wx - 5, wy - 3, wx + 5, wy + 7, SSD1306_WHITE);
  }

  display.drawLine(3, 11, 124, 11, SSD1306_WHITE);

  char timeBuf[12];
  if (is24Hour) {
    sprintf(timeBuf, "%02d:%02d", h, m);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    sprintf(timeBuf, "%02d:%02d", h12, m);
  }

  if (!blinkState) timeBuf[2] = ' ';

  display.setTextSize(3);
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds(timeBuf, 0, 0, &x1, &y1, &tw, &th);
  int timeX = (128 - tw) / 2 - 2;
  display.setCursor(timeX, 14);
  display.print(timeBuf);

  char secBuf[8];
  sprintf(secBuf, ":%02d", sec);
  display.setTextSize(1);
  display.getTextBounds(secBuf, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(128 - tw - 4, 38);
  display.print(secBuf);

  display.setCursor(4, 39);
  display.print(is24Hour ? "24H" : ((h < 12) ? "AM" : "PM"));

  display.drawLine(3, 50, 124, 50, SSD1306_WHITE);
  display.setCursor(4, 54);
  if (wifiOK) {
    display.print("WIFI OK");
  } else if (wifiEnabled) {
    display.print("WIFI ..");
  } else {
    display.print("WIFI OFF");
  }

  display.setCursor(76, 54);
  display.print("Giu 0.65s:MENU");
}

void drawPopupFrame(const char* title) {
  display.drawRoundRect(2, 2, 124, 60, 7, SSD1306_WHITE);
  display.drawRoundRect(5, 5, 118, 54, 5, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(12, 8);
  display.print(title);
  display.drawLine(9, 18, 118, 18, SSD1306_WHITE);
}

void renderMenu() {
  drawPopupFrame("MINI CLOCK  > MENU");
  int first = menuIdx - 1;
  if (first < 0) first = 0;
  if (first > MENU_COUNT - 4) first = MENU_COUNT - 4;
  for (int row = 0; row < 4; row++) {
    int i = first + row;
    if (i >= MENU_COUNT) break;
    int y = 21 + row * 9;
    bool sel = (i == menuIdx);
    if (sel) {
      display.fillRoundRect(8, y - 1, 112, 9, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(13, y);
    display.print(sel ? "> " : "  ");
    display.print(menuItems[i]);
    display.setTextColor(SSD1306_WHITE);
  }
}

void renderGamesMenu() {
  drawPopupFrame("< MENU  /  GAMES");
  int first = gmenuIdx - 1;
  if (first < 0) first = 0;
  if (first > GMENU_COUNT - 4) first = GMENU_COUNT - 4;
  for (int row = 0; row < 4; row++) {
    int i = first + row;
    if (i >= GMENU_COUNT) break;
    int y = 21 + row * 9;
    bool sel = (i == gmenuIdx);
    if (sel) {
      display.fillRoundRect(8, y - 1, 112, 9, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(13, y);
    display.print(sel ? "> " : "  ");
    display.print(gmenuItems[i]);
    display.setTextColor(SSD1306_WHITE);
  }
}

void renderSetTime() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("=== CAI DAT GIO ===");
  char buf[8];
  sprintf(buf, "%02d:%02d", setHour, setMin);
  display.setTextSize(2);
  display.setCursor(34, 20);
  display.print(buf);
  if (setField == 0) display.drawLine(34, 38, 58, 38, SSD1306_WHITE);
  else               display.drawLine(70, 38, 94, 38, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 52);
  display.print("Ngan:+1  Giu:Tiep");
}

void renderSettings() {
  drawPopupFrame("< MENU  /  CAI DAT");
  char buf[10];
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = 21 + i * 9;
    bool sel = (i == settingsIdx);
    if (sel) {
      display.fillRoundRect(8, y - 1, 112, 9, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(12, y);
    display.print(sel ? "> " : "  ");
    display.print(settingsItems[i]);
    if (i == 0) {
      display.setCursor(91, y);
      display.print(is24Hour ? "24h" : "12h");
    } else if (i == 1) {
      display.setCursor(91, y);
      if (screenTimeout == 0) display.print("OFF");
      else { sprintf(buf, "%lus", screenTimeout / 1000); display.print(buf); }
    } else if (i == 2) {
      display.setCursor(91, y);
      display.print(wifiEnabled ? (WiFi.status() == WL_CONNECTED ? "ON" : "..") : "OFF");
    }
    display.setTextColor(SSD1306_WHITE);
  }
}

void renderAbout() {
  drawPopupFrame("< MENU  /  THONG TIN");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(12, 24);
  display.print("Mini Clock ");
  display.print(FIRMWARE_VERSION);
  display.setCursor(12, 36);
  display.print("Tac gia: ");
  display.print(AUTHOR_NAME);
  display.setCursor(12, 49);
  display.print("Giu nut: Quay lai");
}

void renderNumGame() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("=== SO 5 ===");
  if (numState == 0) {
    display.setCursor(8, 20); display.print("Bam khi so = 5");
    display.setCursor(8, 34); display.print("Moi lan dung: +1");
    display.setCursor(8, 50); display.print("Ngan: Bat dau");
  } else if (numState == 1) {
    display.setTextSize(4);
    char b[4]; sprintf(b, "%d", numVal);
    display.setCursor(52, 16);
    display.print(b);
    display.setTextSize(1);
    display.setCursor(4, 56);
    display.print("Diem: "); display.print(numScore);
    display.print("  Best: "); display.print(numBest);
  } else {
    display.setCursor(8, 18); display.print("GAME OVER!");
    display.setCursor(8, 32); display.print("Diem: "); display.print(numScore);
    display.setCursor(8, 44); display.print("Best: "); display.print(numBest);
    display.setCursor(8, 56); display.print("Ngan: Choi lai");
  }
}
