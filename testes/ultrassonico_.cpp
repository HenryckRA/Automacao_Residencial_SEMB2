#include <Arduino.h>
const int TRIG = 14, ECHO = 35;
void setup() { Serial.begin(115200); pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT); }
void loop() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 30000);          // timeout 30 ms
  float cm = us ? us * 0.0343f / 2.0f : -1;
  Serial.printf("distancia: %.1f cm\n", cm);
  delay(300);
}