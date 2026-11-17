#include <Wire.h>

const byte MPU_ADDR = 0x68;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // 電源管理レジスタ
  Wire.write(0);     // スリープ解除
  Wire.endTransmission();
}

void loop() {
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
  float mag = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  Serial.print("x="); Serial.print(a[0]);
  Serial.print(" y="); Serial.print(a[1]);
  Serial.print(" z="); Serial.print(a[2]);
  Serial.print(" motion="); Serial.println(fabs(mag - 9.80665));
  delay(200);
}