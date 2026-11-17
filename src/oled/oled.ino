#include <U8x8lib.h>

U8X8_SSD1306_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);

void setup() {
  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, "OLED OK");
  oled.drawString(0, 2, "Hello!");
}

void loop() {}