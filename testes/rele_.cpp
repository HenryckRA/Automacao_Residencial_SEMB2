#include <Arduino.h>
const int RELE = 27;
const bool ATIVO_ALTO = true;   // se ligar/desligar ao contrário, troque para false
void rele(bool on) {
  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, (on == ATIVO_ALTO) ? HIGH : LOW);
}
void setup() { rele(false); }
void loop() { rele(true); delay(2000); rele(false); delay(2000); }