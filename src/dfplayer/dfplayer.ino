#include <SoftwareSerial.h>
#include "DFRobotDFPlayerMini.h"

SoftwareSerial dfSerial(10, 11);  // RX=D10（DFPlayerのTX）, TX=D11（DFPlayerのRX）
DFRobotDFPlayerMini player;
int track = 1;

void setup() {
  Serial.begin(115200);
  dfSerial.begin(9600);
  Serial.println("Connecting DFPlayer...");
  if (!player.begin(dfSerial)) {
    Serial.println("DFPlayer not found: check wiring and SD card");
    while (true) {}
  }
  player.volume(15);  // 0〜30
  player.play(track);
  Serial.println("Playing track 1");
}

void loop() {
  delay(10000);
  track = track % 4 + 1;
  player.play(track);
  Serial.print("Playing track ");
  Serial.println(track);
}