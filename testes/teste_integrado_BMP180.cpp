// Teste integrado SEMB2 com o sensor GY-68 (BMP180) no lugar do AHT10.
// Mede temperatura e pressão (o BMP180 NÃO mede umidade). Endereço I2C: 0x77.
// Em manual, o push liga/desliga a luz E o ventilador juntos.
// platformio.ini precisa de:  lib_deps = adafruit/Adafruit BMP085 Library  (além das de display que você já usa)
#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h>

// ---------- pinos (mapa do guia) ----------
const int PIN_SDA = 25, PIN_SCL = 26;     // I2C: OLED 0x3C e BMP180 0x77
const int PIN_LDR = 34;                   // divisor LDR + 10 kΩ
const int PIN_PUSH = 32, PIN_TRAVA = 33;  // INPUT_PULLUP, outro lado no GND
const int RELE = 27;                      // base do TIP122 (via 1 kΩ)
const int TRIG = 14, ECHO = 35;           // ECHO passa pelo divisor 1 kΩ + 2 kΩ
const int MOTOR = 13;                     // base do TIP122 do motor (via 1 kΩ)

// ---------- ajustes do teste ----------
const bool ATIVO_ALTO = true;             // com o TIP122 inversor, HIGH liga o relé
const bool COM_MOTOR = true;              // motor ligado (fonte externa na tomada e GND comum)
const bool EXIGE_ESCURO = true;           // false = a luz ignora o LDR
const int LDR_ESCURO = 1000;              // abaixo disso = escuro (seus valores: 300 escuro, 1900 sala)
const int LDR_CLARO = 1400;               // acima disso = claro (histerese entre os dois)
const float DIST_PRESENCA_CM = 30;        // mais perto que isso = presença
const uint32_t SEM_PRESENCA_MS = 5000;    // no projeto final use 30000 a 60000
const float TEMP_LIGA = 28.0, TEMP_DESLIGA = 27.0;  // ventilador (histerese de 1 °C)

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_BMP085 bmp;                      // a biblioteca serve para BMP085 e BMP180

bool sensorOk = false;
float temp = NAN, pressao = NAN, dist = -1;   // pressão em hPa
int ldr = 0;
bool escuro = false, luz = false, luzManual = false, ventilador = false;
uint32_t ultimaPresenca = 0;

void rele(bool on) {
  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, (on == ATIVO_ALTO) ? HIGH : LOW);
}
void motor(bool on) {
  pinMode(MOTOR, OUTPUT);
  digitalWrite(MOTOR, on ? HIGH : LOW);
}

float lerDistanciaCm() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 25000);           // timeout 25 ms (~4 m)
  return us ? us * 0.0343f / 2.0f : -1;
}

// devolve true uma única vez por aperto do push (com debounce de 30 ms)
bool pushFoiApertado() {
  static bool ultimoLido = false, estavel = false;
  static uint32_t tMudanca = 0;
  bool lido = !digitalRead(PIN_PUSH);
  if (lido != ultimoLido) { tMudanca = millis(); ultimoLido = lido; }
  if (millis() - tMudanca > 30 && lido != estavel) {
    estavel = lido;
    if (estavel) return true;
  }
  return false;
}

// lê o BMP180 (~13 ms); false se o valor for impossível (sensor fora do barramento)
bool lerSensor() {
  float t = bmp.readTemperature();
  float p = bmp.readPressure() / 100.0f;          // Pa -> hPa
  if (isnan(t) || t < -40 || t > 85 || p < 300 || p > 1100) return false;
  temp = t; pressao = p;
  return true;
}

void setup() {
  Serial.begin(115200);
  rele(false);                                    // saídas desligadas antes de tudo
  motor(false);
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);
  pinMode(PIN_PUSH, INPUT_PULLUP); pinMode(PIN_TRAVA, INPUT_PULLUP);
  analogReadResolution(12);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setTimeOut(20);                            // falha de I2C custa no máximo 20 ms (padrão: 50)
  Serial.println("--- varredura I2C (esperado: 0x3C e 0x77) ---");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("achei: 0x%02X\n", a);
  }
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);
  oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 0);
  oled.println("SEMB2"); oled.println("teste integrado"); oled.display();
  sensorOk = bmp.begin(BMP085_STANDARD) && lerSensor();
  if (!sensorOk) Serial.println("BMP180 nao achado (vou tentar de novo a cada 5 s)");
  delay(1000);
}

void loop() {
  uint32_t agora = millis();
  static uint32_t tDist = 0, tLdr = 0, tSensor = 0, tTela = 0, tTentativa = 0;
  static bool manualAnterior = false;

  // ---- botões ----
  bool manual = !digitalRead(PIN_TRAVA);          // trava acionada = modo manual
  bool apertou = pushFoiApertado();               // sempre chamar, para não perder o aperto
  if (manual && !manualAnterior) luzManual = luz; // ao entrar no manual, mantém o estado atual
  manualAnterior = manual;
  if (manual && apertou) luzManual = !luzManual;

  // ---- sensores ----
  if (agora - tDist >= 200) { tDist = agora; dist = lerDistanciaCm(); }
  if (agora - tLdr >= 100) {
    tLdr = agora; ldr = analogRead(PIN_LDR);
    if (ldr < LDR_ESCURO) escuro = true;
    else if (ldr > LDR_CLARO) escuro = false;
  }
  if (!sensorOk) {
    if (agora - tTentativa >= 5000) { tTentativa = agora; sensorOk = bmp.begin(BMP085_STANDARD) && lerSensor(); }
  } else if (agora - tSensor >= 1000) {
    tSensor = agora;
    if (!lerSensor()) { sensorOk = false; temp = NAN; pressao = NAN; }
  }

  // ---- decisão ----
  bool presenca = dist > 0 && dist < DIST_PRESENCA_CM;
  if (presenca) ultimaPresenca = agora;
  bool presencaRecente = ultimaPresenca != 0 && (agora - ultimaPresenca < SEM_PRESENCA_MS);
  if (manual) luz = luzManual;
  else luz = presencaRecente && (escuro || !EXIGE_ESCURO);

  if (manual) ventilador = luzManual;             // manual: o push liga/desliga luz e ventilador juntos
  else if (sensorOk) {
    if (temp > TEMP_LIGA) ventilador = true;
    else if (temp < TEMP_DESLIGA) ventilador = false;
  } else ventilador = false;                      // sem sensor no automático, o ventilador fica desligado

  // ---- saídas ----
  rele(luz);
  motor(COM_MOTOR && ventilador);

  // ---- tela e serial ----
  if (agora - tTela >= 500) {
    tTela = agora;
    oled.clearDisplay(); oled.setTextSize(1);
    oled.setCursor(0, 0);
    if (sensorOk) oled.printf("T:%.1fC P:%.0fhPa", temp, pressao); else oled.print("BMP180: sem resposta");
    oled.setCursor(0, 10); oled.printf("LDR:%d %s", ldr, escuro ? "ESCURO" : "CLARO");
    oled.setCursor(0, 20); oled.printf("Dist:%.1fcm %s", dist, presenca ? "PRESENTE" : "");
    oled.setCursor(0, 30); oled.printf("Modo: %s", manual ? "MANUAL" : "AUTO");
    oled.setCursor(0, 40); oled.printf("Luz:%s  Fan:%s", luz ? "ON" : "OFF", COM_MOTOR ? (ventilador ? "ON" : "OFF") : "n/d");
    oled.setCursor(0, 50); oled.printf("push=%d trava=%d", !digitalRead(PIN_PUSH), !digitalRead(PIN_TRAVA));
    oled.display();
    Serial.printf("T=%.1f P=%.0f LDR=%d dist=%.1f modo=%s luz=%d fan=%d\n",
                  temp, pressao, ldr, dist, manual ? "MANUAL" : "AUTO", luz, ventilador);
  }
}