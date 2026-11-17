#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 sensor;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int beatAvg = 0;
unsigned long lastPrint = 0;

void setup() {
  Serial.begin(115200);
  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    while (true) {}
  }
  sensor.setup();
  sensor.setPulseAmplitudeRed(0x0A);
  sensor.setPulseAmplitudeGreen(0);
  Serial.println("Place your finger");
}

void loop() {
  long ir = sensor.getIR();
  if (checkForBeat(ir)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    float bpm = 60 / (delta / 1000.0);
    if (bpm > 20 && bpm < 255) {
      rates[rateSpot++] = (byte)bpm;
      rateSpot %= RATE_SIZE;
      int sum = 0;
      for (byte i = 0; i < RATE_SIZE; i++) sum += rates[i];
      beatAvg = sum / RATE_SIZE;
    }
  }
  if (millis() - lastPrint >= 500) {
    lastPrint = millis();
    Serial.print("IR=");
    Serial.print(ir);
    Serial.print(" avgBPM=");
    Serial.print(beatAvg);
    if (ir < 50000) Serial.print(" (no finger)");
    Serial.println();
  }
}