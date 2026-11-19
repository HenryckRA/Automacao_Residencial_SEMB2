// Teste integrado SEMB2: display + AHT10 + LDR + botões + HC-SR04 + relé (+ motor opcional)
// Coloque este arquivo em src/main.cpp (e guarde o teste anterior em testes/).
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- pinos (mapa do guia) ----------
const int PIN_SDA = 25, PIN_SCL = 26;     // I2C: OLED 0x3C e AHT10 0x38
const int PIN_LDR = 34;                   // divisor LDR + 10 kΩ
const int PIN_PUSH = 32, PIN_TRAVA = 33;  // INPUT_PULLUP, outro lado no GND
const int RELE = 27;                      // base do TIP122 (via 1 kΩ)
const int TRIG = 14, ECHO = 35;           // ECHO passa pelo divisor 1 kΩ + 2 kΩ
const int MOTOR = 13;                     // base do TIP122 do motor (via 1 kΩ)

// ---------- ajustes do teste ----------
const bool ATIVO_ALTO = true;             // com o TIP122 inversor, HIGH liga o relé
const bool COM_MOTOR = true;             // troque para true quando o motor estiver ligado
const bool EXIGE_ESCURO = true;           // false = a luz ignora o LDR
const int LDR_ESCURO = 2000;              // abaixo disso = escuro (seus valores: 300 escuro, 1900 sala)
const int LDR_CLARO = 2400;               // acima disso = claro (histerese entre os dois)
const float DIST_PRESENCA_CM = 30;        // mais perto que isso = presença
const uint32_t SEM_PRESENCA_MS = 5000;    // no projeto final use 30000 a 60000
const float TEMP_LIGA = 28.0, TEMP_DESLIGA = 27.0;  // ventilador (histerese de 1 °C)

Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// ---------- AHT10 sem biblioteca: a Adafruit_AHTX0 trava o loop (~1 s) quando o sensor falha ----------
const uint8_t AHT_ADDR = 0x38;
bool ahtAguardando = false;               // true entre o comando de medir e a leitura (~80 ms depois)

bool ahtIniciar() {                       // comando de calibração; true se o sensor respondeu (ACK)
  Wire.beginTransmission(AHT_ADDR);
  Wire.write(0xE1); Wire.write(0x08); Wire.write(0x00);
  return Wire.endTransmission() == 0;
}
bool ahtDisparar() {                      // manda medir; o resultado fica pronto em ~75 ms
  Wire.beginTransmission(AHT_ADDR);
  Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);
  return Wire.endTransmission() == 0;
}
bool ahtLer(float &t, float &u) {         // false se faltar byte, se estiver ocupado ou se vier tudo zero
  if (Wire.requestFrom((int)AHT_ADDR, 6) != 6) return false;
  uint8_t d[6];
  for (int i = 0; i < 6; i++) d[i] = Wire.read();
  if (d[0] & 0x80) return false;
  uint32_t h  = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t tr = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  if (h == 0 && tr == 0) return false;    // era o caso do T=-50 U=0
  u = h * 100.0f / 1048576.0f;
  t = tr * 200.0f / 1048576.0f - 50.0f;
  return true;
}

bool ahtOk = false;
float temp = NAN, umid = NAN, dist = -1;
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

void setup() {
  Serial.begin(115200);
  rele(false);                                    // saídas desligadas antes de tudo
  motor(false);
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);
  pinMode(PIN_PUSH, INPUT_PULLUP); pinMode(PIN_TRAVA, INPUT_PULLUP);
  analogReadResolution(12);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setTimeOut(20);                            // falha de I2C custa no máximo 20 ms (padrão: 50)
  Serial.println("--- varredura I2C (esperado: 0x38 e 0x3C) ---");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("achei: 0x%02X\n", a);
  }
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);
  oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 0);
  oled.println("SEMB2"); oled.println("teste integrado"); oled.display();
  delay(40);                                      // AHT10 precisa de ~40 ms depois de energizar
  ahtOk = ahtIniciar();
  if (!ahtOk) Serial.println("AHT10 nao achado (vou tentar de novo a cada 5 s)");
  delay(1000);
}

void loop() {
  uint32_t agora = millis();
  static uint32_t tDist = 0, tLdr = 0, tAht = 0, tTela = 0, tAhtTentativa = 0;
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
  // AHT10 em duas fases, sem delay: 1) manda medir; 2) ~80 ms depois lê. O loop nunca espera pelo sensor.
  if (!ahtOk) {
    if (agora - tAhtTentativa >= 5000) { tAhtTentativa = agora; ahtOk = ahtIniciar(); ahtAguardando = false; }
  } else if (!ahtAguardando) {
    if (agora - tAht >= 1000) {
      tAht = agora;
      if (ahtDisparar()) ahtAguardando = true;
      else { ahtOk = false; temp = NAN; umid = NAN; }
    }
  } else if (agora - tAht >= 80) {
    ahtAguardando = false;
    if (!ahtLer(temp, umid)) { ahtOk = false; temp = NAN; umid = NAN; }
  }

  // ---- decisão ----
  bool presenca = dist > 0 && dist < DIST_PRESENCA_CM;
  if (presenca) ultimaPresenca = agora;
  bool presencaRecente = ultimaPresenca != 0 && (agora - ultimaPresenca < SEM_PRESENCA_MS);
  if (manual) luz = luzManual;
  else luz = presencaRecente && (escuro || !EXIGE_ESCURO);

  if (COM_MOTOR && ahtOk) {
    if (temp > TEMP_LIGA) ventilador = true;
    else if (temp < TEMP_DESLIGA) ventilador = false;
  }

  // ---- saídas ----
  rele(luz);
  motor(COM_MOTOR && ventilador);

  // ---- tela e serial ----
  if (agora - tTela >= 500) {
    tTela = agora;
    oled.clearDisplay(); oled.setTextSize(1);
    oled.setCursor(0, 0);
    if (ahtOk) oled.printf("T:%.1fC  U:%.0f%%", temp, umid); else oled.print("AHT10: sem resposta");
    oled.setCursor(0, 10); oled.printf("LDR:%d %s", ldr, escuro ? "ESCURO" : "CLARO");
    oled.setCursor(0, 20); oled.printf("Dist:%.1fcm %s", dist, presenca ? "PRESENTE" : "");
    oled.setCursor(0, 30); oled.printf("Modo: %s", manual ? "MANUAL" : "AUTO");
    oled.setCursor(0, 40); oled.printf("Luz:%s  Fan:%s", luz ? "ON" : "OFF", COM_MOTOR ? (ventilador ? "ON" : "OFF") : "n/d");
    oled.setCursor(0, 50); oled.printf("push=%d trava=%d", !digitalRead(PIN_PUSH), !digitalRead(PIN_TRAVA));
    oled.display();
    Serial.printf("T=%.1f U=%.0f LDR=%d dist=%.1f modo=%s luz=%d fan=%d\n",
                  temp, umid, ldr, dist, manual ? "MANUAL" : "AUTO", luz, ventilador);
  }
}
