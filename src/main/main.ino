// センサー×MP3再生回路 本体スケッチ ver3
// 心拍と動きから「感情」を判定し、感情ごとのフォルダからランダムに曲を流す
#include <Wire.h>
#include <SoftwareSerial.h>
#include <U8x8lib.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "DFRobotDFPlayerMini.h"

// ===== 調整用の設定値 =====
const int   HR_THRESHOLD   = 90;     // 心拍の閾値（BPM）
const float MOVE_THRESHOLD = 2.0;    // 動きの閾値（m/s^2）
const unsigned long DECIDE_INTERVAL  = 10000;  // 感情の判定間隔（ミリ秒）
const unsigned long DISPLAY_INTERVAL = 1000;   // 画面の更新間隔（ミリ秒）
const unsigned long MOTION_INTERVAL  = 50;     // 加速度の読み取り間隔（ミリ秒）
const unsigned long FINISH_GUARD     = 1000;   // 再生開始直後の「再生終了」通知は無視する（ミリ秒）
const int   VOLUME         = 15;     // 音量（0〜30）
const long  FINGER_IR_MIN  = 50000;  // これ未満なら「指なし」

// ===== 感情の種類 =====
// 心拍（低/高）× 動き（少/多）の4区分。SDカードのフォルダ番号 = 感情の番号 + 1
//   01/ … Relaxed  心拍が低く、動きが少ない（落ち着いている）
//   02/ … Happy    心拍が低く、動きが多い（余裕をもって楽しく動いている）
//   03/ … Tense    心拍が高く、動きが少ない（緊張・ドキドキ）
//   04/ … Excited  心拍が高く、動きが多い（高揚・エネルギッシュ）
// 画面に出す名前は英数字のみ（日本語フォントはUnoのメモリに入らない）
const byte EMOTION_COUNT = 4;
const char* const EMOTION_NAMES[EMOTION_COUNT] = {"Relaxed", "Happy", "Tense", "Excited"};
const byte NO_EMOTION = 255;  // まだ何も再生していない

// 1フォルダあたりの最大曲数（再生済みの記録に MAX_SONGS/8 バイト × 4 を使う）
const byte MAX_SONGS = 100;

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

// ===== 選曲用（感情ごと） =====
byte songCount[EMOTION_COUNT];                     // フォルダ内の曲数
byte played[EMOTION_COUNT][(MAX_SONGS + 7) / 8];   // 今の一巡で再生済みの曲（ビット）
byte playedCount[EMOTION_COUNT];                   // 今の一巡で再生済みの曲数
byte lastSong[EMOTION_COUNT];                      // 最後に流した曲番号（0 = なし）

// ===== 状態 =====
byte currentEmotion = NO_EMOTION;
byte currentSong = 0;  // フォルダ内の曲番号（1〜）
unsigned long songStartedAt = 0;
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

  if (!loadSongCounts()) {
    showError("No songs on SD");
  }
  randomSeed(makeSeed());

  oled.clear();
  updateDisplay();
}

void loop() {
  unsigned long now = millis();

  readHeart();  // 毎回呼ぶ（拍の取りこぼしを防ぐ）
  checkPlayer();

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
    decideEmotion();
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

// 心拍と動きから感情の番号（0〜3）を決める
byte classifyEmotion(int hr, float motion) {
  bool hrHigh = (hr >= HR_THRESHOLD);
  bool moveHigh = (motion >= MOVE_THRESHOLD);
  return (hrHigh ? 2 : 0) + (moveHigh ? 1 : 0);
}

// 10秒ごとに感情を判定し、変わったらそのフォルダの曲に切り替える
void decideEmotion() {
  float motionAvg = (motionCount > 0) ? motionSum / motionCount : 0;
  motionSum = 0;
  motionCount = 0;

  Serial.print(F("HR="));
  Serial.print(beatAvg);
  Serial.print(F(" motion="));
  Serial.print(motionAvg);

  // 指なし、または心拍の測定中は今の曲を続ける
  if (!fingerOn || beatAvg == 0) {
    Serial.println(F(" -> keep"));
    return;
  }

  byte emotion = classifyEmotion(beatAvg, motionAvg);
  Serial.print(F(" -> "));
  Serial.println(EMOTION_NAMES[emotion]);

  if (emotion != currentEmotion) {
    playRandomSong(emotion);
  }
}

// DFPlayerからの通知を確認し、曲が終わったら同じ感情の次の曲を流す
void checkPlayer() {
  if (!player.available()) return;
  byte type = player.readType();
  if (type == DFPlayerPlayFinished) {
    // 終了通知は2回届くことがあるため、再生開始直後のものは無視する
    if (currentEmotion != NO_EMOTION && millis() - songStartedAt >= FINISH_GUARD) {
      playRandomSong(currentEmotion);
    }
  } else if (type == DFPlayerError) {
    Serial.print(F("DFPlayer error code="));
    Serial.println(player.read());
  }
}

// 指定した感情のフォルダから、まだ流していない曲をランダムに選んで再生する
void playRandomSong(byte emotion) {
  byte song = pickSong(emotion);
  if (song == 0) {
    Serial.print(F("No songs in folder "));
    Serial.println(emotion + 1);
    return;  // フォルダが空なら今の曲を続ける
  }
  player.playFolder(emotion + 1, song);
  songStartedAt = millis();
  currentEmotion = emotion;
  currentSong = song;

  Serial.print(F("Play "));
  Serial.print(emotion + 1);
  Serial.print('/');
  Serial.println(song);
  updateDisplay();
}

// シャッフル選曲：一巡するまで同じ曲は選ばない。曲番号（1〜）を返す。0 = 曲なし
byte pickSong(byte e) {
  byte n = songCount[e];
  if (n == 0) return 0;

  // 全曲流し終えたら次の一巡へ
  if (playedCount[e] >= n) {
    memset(played[e], 0, sizeof(played[e]));
    playedCount[e] = 0;
  }

  // 一巡の最初の1曲だけは、直前に流した曲を避ける（同じ曲の連続を防ぐ）
  byte skip = (playedCount[e] == 0 && n > 1) ? lastSong[e] : 0;

  // 未再生の曲の中から k 番目を選ぶ
  byte k = random(n - playedCount[e] - (skip ? 1 : 0));
  for (byte s = 1; s <= n; s++) {
    if (isPlayed(e, s) || s == skip) continue;
    if (k == 0) {
      markPlayed(e, s);
      lastSong[e] = s;
      return s;
    }
    k--;
  }
  return 0;  // ここには来ない
}

bool isPlayed(byte e, byte song) {
  byte i = song - 1;
  return played[e][i / 8] & (1 << (i % 8));
}

void markPlayed(byte e, byte song) {
  byte i = song - 1;
  played[e][i / 8] |= (1 << (i % 8));
  playedCount[e]++;
}

// 各感情フォルダの曲数をDFPlayerに問い合わせる。1曲以上あれば true
bool loadSongCounts() {
  bool any = false;
  for (byte e = 0; e < EMOTION_COUNT; e++) {
    int count = -1;
    for (byte retry = 0; retry < 3 && count < 0; retry++) {
      count = player.readFileCountsInFolder(e + 1);
      if (count < 0) delay(100);
    }
    if (count < 0) count = 0;
    if (count > MAX_SONGS) {
      Serial.print(F("Too many songs, using first "));
      Serial.println(MAX_SONGS);
      count = MAX_SONGS;
    }
    songCount[e] = count;
    if (count > 0) any = true;

    Serial.print(F("Folder "));
    Serial.print(e + 1);
    Serial.print(F(" ("));
    Serial.print(EMOTION_NAMES[e]);
    Serial.print(F("): "));
    Serial.print(count);
    Serial.println(F(" songs"));
  }
  return any;
}

// 乱数の種：未接続のアナログピン（A0〜A3）のノイズと起動時間を混ぜる
// A4・A5はI2Cで使っているので読まない
unsigned long makeSeed() {
  unsigned long seed = micros();
  for (byte i = 0; i < 4; i++) {
    seed = (seed << 5) ^ (seed >> 27) ^ analogRead(A0 + i);
  }
  return seed;
}

// 画面を更新する
void updateDisplay() {
  oled.clearLine(0);
  oled.setCursor(0, 0);
  oled.print("Mood: ");
  oled.print(currentEmotion == NO_EMOTION ? "---" : EMOTION_NAMES[currentEmotion]);

  oled.clearLine(1);
  oled.setCursor(0, 1);
  if (currentEmotion != NO_EMOTION) {
    oled.print("Song: ");
    oled.print(currentSong);
    oled.print('/');
    oled.print(songCount[currentEmotion]);
  }

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
