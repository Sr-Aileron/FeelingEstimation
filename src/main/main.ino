// センサー×MP3再生回路 本体スケッチ ver2
#include <Wire.h>
#include <SoftwareSerial.h>
#include <U8x8lib.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "DFRobotDFPlayerMini.h"

// ===== 調整用の設定値 =====
const int   HR_THRESHOLD   = 90;     // 心拍の閾値（BPM）
const float MOVE_THRESHOLD = 2.0;    // 動きの閾値（m/s^2）
const unsigned long DECIDE_INTERVAL  = 10000;  // 曲の判定間隔（ミリ秒）
const unsigned long DISPLAY_INTERVAL = 1000;   // 画面の更新間隔（ミリ秒）
const unsigned long MOTION_INTERVAL  = 50;     // 加速度の読み取り間隔（ミリ秒）
const int   VOLUME         = 15;     // 音量（0〜30）
const long  FINGER_IR_MIN  = 50000;  // これ未満なら「指なし」

const char* const TRACK_NAMES[] = {"track1", "track2", "track3", "track4"};

// ===== 部品 =====
MAX30105 heartSensor;
U8X8_SSD1306_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);
SoftwareSerial dfSerial(10, 11);  // RX=D10（DFPlayerのTX）, TX=D11（DFPlayerのRX）
DFRobotDFPlayerMini player;
const byte MPU_ADDR = 0x68;

// ===== 心拍の計算用 =====
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
byte validBeats = 0;
long lastBeat = 0;
int beatAvg = 0;       // 0 = まだ測定できていない
bool fingerOn = false;

// ===== 動きの計算用 =====
float motionSum = 0;
unsigned int motionCount = 0;

// ===== 状態 =====
int currentTrack = 0;  // 0 = まだ再生していない
unsigned long lastDecide = 0, lastDisplay = 0, lastMotion = 0;

void setup() {
  Serial.begin(115200);

  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, "Starting...");

  if (!heartSensor.begin(Wire, I2C_SPEED_FAST)) {
    showError("MAX30102 error");
  }
  heartSensor.setup();
  heartSensor.setPulseAmplitudeRed(0x0A);
  heartSensor.setPulseAmplitudeGreen(0);

  mpuInit();

  dfSerial.begin(9600);
  if (!player.begin(dfSerial)) {
    showError("DFPlayer error");
  }
  player.volume(VOLUME);

  oled.clear();
  updateDisplay();
}

void loop() {
  unsigned long now = millis();

  readHeart();  // 毎回呼ぶ（拍の取りこぼしを防ぐ）

  if (now - lastMotion >= MOTION_INTERVAL) {
    lastMotion = now;
    readMotion();
  }
  if (now - lastDisplay >= DISPLAY_INTERVAL) {
    lastDisplay = now;
    updateDisplay();
  }
  if (now - lastDecide >= DECIDE_INTERVAL) {
    lastDecide = now;
    decideTrack();
  }
}

// 心拍：拍の間隔からBPMを計算し、直近4拍で平均する
void readHeart() {
  long ir = heartSensor.getIR();
  fingerOn = (ir >= FINGER_IR_MIN);
  if (!fingerOn) {
    validBeats = 0;  // 指を離したら測り直す
    beatAvg = 0;
    return;
  }
  if (checkForBeat(ir)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    float bpm = 60 / (delta / 1000.0);
    if (bpm > 20 && bpm < 255) {
      rates[rateSpot++] = (byte)bpm;
      rateSpot %= RATE_SIZE;
      if (validBeats < RATE_SIZE) validBeats++;
      if (validBeats >= RATE_SIZE) {
        int sum = 0;
        for (byte i = 0; i < RATE_SIZE; i++) sum += rates[i];
        beatAvg = sum / RATE_SIZE;
      }
    }
  }
}

// 加速度センサーを起動する
void mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // 電源管理レジスタ
  Wire.write(0);     // スリープ解除
  Wire.endTransmission();
}

// 加速度の大きさ（m/s^2）を返す
float readAccelMagnitude() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // 加速度データの先頭レジスタ
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (byte)6);
  float a[3];
  for (byte i = 0; i < 3; i++) {
    byte hi = Wire.read();
    byte lo = Wire.read();
    int16_t raw = (int16_t)((hi << 8) | lo);
    a[i] = raw / 16384.0 * 9.80665;  // ±2g設定 → m/s^2
  }
  return sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

// 動きの量（重力分を引いた絶対値）を積算する
void readMotion() {
  motionSum += fabs(readAccelMagnitude() - 9.80665);
  motionCount++;
}

// 10秒ごとに曲を判定する
void decideTrack() {
  float motionAvg = (motionCount > 0) ? motionSum / motionCount : 0;
  motionSum = 0;
  motionCount = 0;

  Serial.print("HR=");
  Serial.print(beatAvg);
  Serial.print(" motion=");
  Serial.print(motionAvg);

  // 指なし、または心拍の測定中は今の曲を続ける
  if (!fingerOn || beatAvg == 0) {
    Serial.println(" -> keep");
    return;
  }

  bool hrHigh = (beatAvg >= HR_THRESHOLD);
  bool moveHigh = (motionAvg >= MOVE_THRESHOLD);
  int track = 1 + (hrHigh ? 2 : 0) + (moveHigh ? 1 : 0);
  Serial.print(" -> track");
  Serial.println(track);

  if (track != currentTrack) {
    player.loop(track);  // 指定した曲をループ再生
    currentTrack = track;
    updateDisplay();
  }
}

// 画面を更新する
void updateDisplay() {
  oled.clearLine(0);
  oled.setCursor(0, 0);
  oled.print("Song: ");
  oled.print(currentTrack == 0 ? "---" : TRACK_NAMES[currentTrack - 1]);

  oled.clearLine(2);
  oled.setCursor(0, 2);
  if (!fingerOn) {
    oled.print("Place finger");
  } else if (beatAvg == 0) {
    oled.print("HR: measuring");
  } else {
    oled.print("HR: ");
    oled.print(beatAvg);
    oled.print(" bpm");
  }
}

// エラーを表示して止まる
void showError(const char* msg) {
  oled.clear();
  oled.drawString(0, 0, msg);
  Serial.println(msg);
  while (true) {}
}