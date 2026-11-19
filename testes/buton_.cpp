#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  pinMode(32, INPUT_PULLUP);
  pinMode(33, INPUT_PULLUP);
}
void loop() {
  Serial.printf("push=%d  trava=%d\n", !digitalRead(32), !digitalRead(33));  // 1 = ativo
  delay(200);
}