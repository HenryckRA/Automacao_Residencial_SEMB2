// Teste bobo: o HC-SR04 aponta para a parede; ao pôr o dedo na frente, o display mostra
// MENGAO CAMPEAO e o LED (pelo relé) acende. Coloque em src/main.cpp.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const int TRIG = 14, ECHO = 35, RELE = 27;
const bool ATIVO_ALTO = true;      // com o TIP122 inversor, HIGH liga o relé
const float MARGEM_CM = 5;         // quanto mais perto que a parede conta como "dedo"

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
float parede = -1;                 // distância medida até a parede (cm)
uint32_t ultimoDedo = 0;

void rele(bool on) {
  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, (on == ATIVO_ALTO) ? HIGH : LOW);
}

float lerCm() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 30000);          // timeout 30 ms
  return us ? us * 0.0343f / 2.0f : -1;
}

// média de até 10 leituras válidas da parede (sem o dedo na frente)
void calibrar() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 0);
  oled.println("Calibrando...");
  oled.println("Nao ponha o dedo");
  oled.display();
  float soma = 0; int n = 0;
  for (int i = 0; i < 30 && n < 10; i++) {
    float d = lerCm();
    if (d > 0) { soma += d; n++; }
    delay(100);
  }
  parede = n ? soma / n : -1;
  Serial.printf("parede a %.1f cm\n", parede);
}

void setup() {
  Serial.begin(115200);
  rele(false);
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);
  Wire.begin(25, 26);
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);
  calibrar();
}

void loop() {
  if (parede <= 0) { calibrar(); return; }       // sem eco na calibração: tenta de novo

  float d = lerCm();
  bool dedo = d > 0 && d < parede - MARGEM_CM;
  if (dedo) ultimoDedo = millis();
  bool ativo = ultimoDedo != 0 && (millis() - ultimoDedo < 600);   // segura 0,6 s para não piscar
  rele(ativo);

  oled.clearDisplay();
  if (ativo) {
    oled.setTextSize(2);
    oled.setCursor(0, 10); oled.println("MENGAO");
    oled.println("CAMPEAO");
  } else {
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.printf("Parede: %.1f cm\n", parede);
    oled.printf("Agora:  %.1f cm\n", d);
    oled.println("Ponha o dedo na frente");
  }
  oled.display();
  delay(100);
}
