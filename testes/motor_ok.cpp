#include <Arduino.h>
const int MOTOR = 13;
void setup() { pinMode(MOTOR, OUTPUT); }
void loop() {
  digitalWrite(MOTOR, HIGH); delay(2000);   // gira 2 s
  digitalWrite(MOTOR, LOW);  delay(3000);   // para 3 s
}