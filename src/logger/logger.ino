// 記録用スケッチ：心拍センサーの生データと加速度・角速度を CSV でシリアル出力する
// 感情判定のロジックと閾値を、実測データから決めるために使う
//   書き込み: make upload SKETCH=logger
//   記録:     make record LABEL=still DURATION=120
//
// 出力形式（1行 = 心拍センサーの1サンプル、約100Hz）
//   ms    … 起動からの時間（ミリ秒）
//   ir    … 心拍センサーの赤外光の強さ（指なしなら 50000 未満）
//   ax〜az … 加速度の生値（±8g、4096 = 1g）
//   gx〜gz … 角速度の生値（±500°/s、65.5 = 1°/s）
//   beat  … main と同じ checkForBeat() が拍を検出したら 1
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// ===== 部品 =====
MAX30105 heartSensor;
const byte MPU_ADDR = 0x68;

// ===== 最新の加速度・角速度 =====
int16_t imu[6];  // ax, ay, az, gx, gy, gz
unsigned long lastImu = 0;
const unsigned long IMU_INTERVAL = 10;  // 加速度の読み取り間隔（ミリ秒）

void setup() {
  Serial.begin(115200);

  // 心拍センサーは main と同じ設定にする（記録したデータを main の判定にそのまま使えるように）
  if (!heartSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("# error: MAX30102 not found"));
    while (true) {}
  }
  heartSensor.setup();
  heartSensor.setPulseAmplitudeRed(0x0A);
  heartSensor.setPulseAmplitudeGreen(0);

  if (!mpuInit()) {
    Serial.println(F("# error: MPU-6050 not found"));
    while (true) {}
  }

  Serial.println(F("# logger v1 accel=8g(4096/g) gyro=500dps(65.5/dps)"));
  Serial.println(F("ms,ir,ax,ay,az,gx,gy,gz,beat"));
}

void loop() {
  unsigned long now = millis();
  if (now - lastImu >= IMU_INTERVAL) {
    lastImu = now;
    readImu();
  }

  // FIFO にたまった心拍センサーのサンプルを取りこぼさず全部出す
  heartSensor.check();
  while (heartSensor.available()) {
    long ir = heartSensor.getFIFOIR();
    heartSensor.nextSample();
    bool beat = checkForBeat(ir);
    printRow(millis(), ir, beat);
  }
}

// 加速度センサーを起動し、測定範囲を広げる。見つからなければ false
bool mpuInit() {
  if (!writeMpu(0x6B, 0)) return false;  // 電源管理：スリープ解除
  writeMpu(0x1A, 0x03);  // ローパスフィルタ 約44Hz（振動ノイズを抑える）
  writeMpu(0x1B, 0x08);  // 角速度 ±500°/s
  writeMpu(0x1C, 0x10);  // 加速度 ±8g（腕を振ると ±2g では振り切れるため）
  return true;
}

bool writeMpu(byte reg, byte value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// 加速度・温度・角速度の14バイトをまとめて読む（温度は捨てる）
void readImu() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // 加速度データの先頭レジスタ
  Wire.endTransmission(false);
  if (Wire.requestFrom(MPU_ADDR, (byte)14) != 14) return;  // 読めなければ前回の値のまま
  int16_t v[7];
  for (byte i = 0; i < 7; i++) {
    byte hi = Wire.read();
    byte lo = Wire.read();
    v[i] = (int16_t)((hi << 8) | lo);
  }
  for (byte i = 0; i < 3; i++) imu[i] = v[i];          // 加速度
  for (byte i = 0; i < 3; i++) imu[3 + i] = v[4 + i];  // 角速度（v[3] は温度）
}

void printRow(unsigned long ms, long ir, bool beat) {
  Serial.print(ms);
  Serial.print(',');
  Serial.print(ir);
  for (byte i = 0; i < 6; i++) {
    Serial.print(',');
    Serial.print(imu[i]);
  }
  Serial.print(',');
  Serial.println(beat ? 1 : 0);
}
