// teste 2b: AHT10 + OLED
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AHTX0.h>
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_AHTX0 aht;
void setup() {
  Serial.begin(115200);
  Wire.begin(25, 26);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED nao achado");
  if (!aht.begin())                           Serial.println("AHT10 nao achado");
  oled.setTextColor(SSD1306_WHITE);
}
void loop() {
  sensors_event_t h, t;
  aht.getEvent(&h, &t);
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(0, 0);  oled.printf("%.1f C", t.temperature);
  oled.setCursor(0, 28); oled.printf("%.0f %%", h.relative_humidity);
  oled.display();
  Serial.printf("T=%.1f  U=%.0f\n", t.temperature, h.relative_humidity);
  delay(1000);
}