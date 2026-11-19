// SEMB2 - automação de uma sala: sensores + display + Wi-Fi/MQTT (broker Mosquitto, painel Node-RED).
// Coloque em src/main.cpp. Crie include/segredos.h a partir do segredos_exemplo.h (Wi-Fi e IP do PC).
//
// Dois "núcleos" de trabalho, para a rede nunca travar o controle:
//   - loop()        (núcleo 1): botões, sensores, decisão, relé, motor e display. Nunca espera pela rede.
//   - tarefaRede()  (núcleo 0): Wi-Fi + MQTT (conecta, publica, recebe comandos).
// O connect do MQTT pode bloquear por vários segundos se o broker estiver fora; por isso fica na tarefa de rede.
//
// platformio.ini:  lib_deps = adafruit/Adafruit SSD1306
//                             adafruit/Adafruit GFX Library
//                             adafruit/Adafruit BMP085 Library
//                             256dpi/MQTT
#include <Arduino.h>
#include <math.h>
#include <atomic>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h>
#include <MQTT.h>
#include "segredos.h"                     // WIFI_SSID, WIFI_SENHA, MQTT_HOST

// ---------- pinos (mapa do guia) ----------
const int PIN_SDA = 25, PIN_SCL = 26;     // I2C: OLED 0x3C e BMP180 0x77
const int PIN_LDR = 34;                   // divisor LDR + 10 kΩ
const int PIN_PUSH = 32, PIN_TRAVA = 33;  // INPUT_PULLUP, outro lado no GND
const int RELE = 27;                      // base do TIP122 (via 1 kΩ)
const int TRIG = 14, ECHO = 35;           // ECHO passa pelo divisor 1 kΩ + 2 kΩ
const int MOTOR = 13;                     // base do TIP122 do motor (via 1 kΩ)

// ---------- ajustes ----------
const bool ATIVO_ALTO = true;             // com o TIP122 inversor, HIGH liga o relé
const bool COM_MOTOR = true;              // motor ligado (fonte externa na tomada e GND comum)
const bool EXIGE_ESCURO = true;           // false = a luz ignora o LDR
const int LDR_ESCURO = 1000;              // abaixo disso = escuro
const int LDR_CLARO = 1400;               // acima disso = claro (histerese entre os dois)
const float DIST_PRESENCA_CM = 30;        // mais perto que isso = presença
const uint32_t SEM_PRESENCA_MS = 5000;    // no projeto final use 30000 a 60000
const float TEMP_LIGA = 28.0, TEMP_DESLIGA = 27.0;  // ventilador (histerese de 1 °C)
const uint32_t PUBLICA_SENSORES_MS = 2000;

// ---------- tópicos MQTT ----------
const char T_STATUS[]    = "semb2/sala/status";            // online/offline (retido; offline = Last Will)
const char T_TEMP[]      = "semb2/sala/temperatura";       // °C
const char T_PRESSAO[]   = "semb2/sala/pressao";           // hPa
const char T_LDR[]       = "semb2/sala/luminosidade";      // 0-4095
const char T_DIST[]      = "semb2/sala/distancia";         // cm
const char T_PRESENCA[]  = "semb2/sala/presenca";          // ON/OFF
const char T_LUZ_EST[]   = "semb2/sala/luz/estado";        // ON/OFF (retido)
const char T_VENT_EST[]  = "semb2/sala/ventilador/estado"; // ON/OFF (retido)
const char T_MODO_EST[]  = "semb2/sala/modo/estado";       // AUTO/MANUAL (retido)
const char T_LUZ_SET[]   = "semb2/sala/luz/set";           // comando: ON/OFF
const char T_VENT_SET[]  = "semb2/sala/ventilador/set";    // comando: ON/OFF
const char T_MODO_SET[]  = "semb2/sala/modo/set";          // comando: AUTO/MANUAL

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_BMP085 bmp;                      // a biblioteca serve para BMP085 e BMP180
WiFiClient rede;
MQTTClient mqtt(256);

// ---------- estado compartilhado entre os dois núcleos ----------
// escritos só pelo loop(), lidos pela tarefa de rede (valores de 32 bits: leitura segura no ESP32)
volatile float gTemp = NAN, gPressao = NAN, gDist = -1;
volatile int gLdr = 0;
volatile bool gPresenca = false, gLuz = false, gVent = false, gManual = false;
// escritos só pela tarefa de rede, lidos pelo loop()
volatile bool gMqttOk = false;
std::atomic<int> cmdLuz{-1}, cmdVent{-1}, cmdModo{-1};   // -1 = sem comando

// ---------- estado do controle (só o loop() mexe) ----------
bool sensorOk = false;
float temp = NAN, pressao = NAN, dist = -1;
int ldr = 0;
bool escuro = false, luz = false, ventilador = false;
bool luzManual = false, ventManual = false, modoRemoto = false;
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

// =====================================================================
//                         REDE (núcleo 0)
// =====================================================================
void aoReceber(String &topic, String &payload) {
  payload.trim();
  payload.toUpperCase();
  int v = -1;                                      // ON/OFF
  if (payload == "ON" || payload == "1" || payload == "TRUE") v = 1;
  else if (payload == "OFF" || payload == "0" || payload == "FALSE") v = 0;

  if (topic == T_LUZ_SET) cmdLuz = v;
  else if (topic == T_VENT_SET) cmdVent = v;
  else if (topic == T_MODO_SET) cmdModo = (payload == "MANUAL") ? 1 : (payload == "AUTO") ? 0 : -1;
  Serial.printf("MQTT <- %s = %s\n", topic.c_str(), payload.c_str());
}

void pub(const char *topico, const char *valor, bool retido = false) {
  mqtt.publish(topico, valor, retido, 0);
}

// estados (luz, ventilador, modo, presença) só quando mudam, ou todos se "todos" for true
void publicarEstados(bool todos) {
  static int ultLuz = -1, ultVent = -1, ultModo = -1, ultPres = -1;
  bool l = gLuz, v = gVent, m = gManual, p = gPresenca;
  if (todos || (int)l != ultLuz)   { pub(T_LUZ_EST, l ? "ON" : "OFF", true);        ultLuz = l; }
  if (todos || (int)v != ultVent)  { pub(T_VENT_EST, v ? "ON" : "OFF", true);       ultVent = v; }
  if (todos || (int)m != ultModo)  { pub(T_MODO_EST, m ? "MANUAL" : "AUTO", true);  ultModo = m; }
  if (todos || (int)p != ultPres)  { pub(T_PRESENCA, p ? "ON" : "OFF");             ultPres = p; }
}

void publicarSensores() {
  float t = gTemp, pr = gPressao, d = gDist;
  if (!isnan(t))  pub(T_TEMP, String(t, 1).c_str());
  if (!isnan(pr)) pub(T_PRESSAO, String(pr, 0).c_str());
  pub(T_LDR, String((int)gLdr).c_str());
  if (d > 0)      pub(T_DIST, String(d, 1).c_str());
}

void tarefaRede(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  mqtt.begin(MQTT_HOST, 1883, rede);
  mqtt.onMessage(aoReceber);
  mqtt.setWill(T_STATUS, "offline", true, 1);      // o broker publica "offline" se o ESP32 sumir
  uint32_t tTentativa = 0, tSensores = 0;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected()) {
        gMqttOk = false;
        if (millis() - tTentativa > 5000) {        // uma tentativa a cada 5 s
          tTentativa = millis();
          Serial.printf("MQTT: conectando em %s ...\n", MQTT_HOST);
          if (mqtt.connect("esp32-semb2")) {
            mqtt.subscribe(T_LUZ_SET);
            mqtt.subscribe(T_VENT_SET);
            mqtt.subscribe(T_MODO_SET);
            pub(T_STATUS, "online", true);
            publicarEstados(true);
            gMqttOk = true;
            Serial.println("MQTT: conectado");
          } else {
            Serial.printf("MQTT: falhou (erro %d)\n", (int)mqtt.lastError());
          }
        }
      } else {
        gMqttOk = true;
        mqtt.loop();
        publicarEstados(false);
        if (millis() - tSensores >= PUBLICA_SENSORES_MS) { tSensores = millis(); publicarSensores(); }
      }
    } else {
      gMqttOk = false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================================================
//                     CONTROLE E DISPLAY (núcleo 1)
// =====================================================================
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
  oled.println("SEMB2"); oled.println("Wi-Fi + MQTT"); oled.display();
  sensorOk = bmp.begin(BMP085_STANDARD) && lerSensor();
  if (!sensorOk) Serial.println("BMP180 nao achado (vou tentar de novo a cada 5 s)");

  xTaskCreatePinnedToCore(tarefaRede, "rede", 10240, nullptr, 1, nullptr, 0);
  delay(500);
}

void loop() {
  uint32_t agora = millis();
  static uint32_t tDist = 0, tLdr = 0, tSensor = 0, tTela = 0, tTentativa = 0;
  static bool manualAnterior = false;

  // ---- comandos vindos do MQTT (a tarefa de rede só guarda; quem decide é o controle) ----
  int cL = cmdLuz.exchange(-1), cV = cmdVent.exchange(-1), cM = cmdModo.exchange(-1);
  if (cM == 0) modoRemoto = false;                // AUTO
  if (cM == 1) modoRemoto = true;                 // MANUAL

  // ---- botões ----
  bool trava = !digitalRead(PIN_TRAVA);           // trava acionada = modo manual
  bool apertou = pushFoiApertado();               // sempre chamar, para não perder o aperto
  bool manual = trava || modoRemoto;
  if (manual && !manualAnterior) { luzManual = luz; ventManual = ventilador; }  // entra mantendo o estado atual
  manualAnterior = manual;
  // um comando de luz/ventilador vindo do painel também coloca em manual
  auto garantirManual = [&]() {
    if (!manual) { manual = true; manualAnterior = true; modoRemoto = true; luzManual = luz; ventManual = ventilador; }
  };
  if (cL >= 0) { garantirManual(); luzManual = cL; }
  if (cV >= 0) { garantirManual(); ventManual = cV; }
  if (manual && apertou) { luzManual = !luzManual; ventManual = luzManual; }

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

  if (manual) ventilador = ventManual;
  else if (sensorOk) {
    if (temp > TEMP_LIGA) ventilador = true;
    else if (temp < TEMP_DESLIGA) ventilador = false;
  } else ventilador = false;                      // sem sensor no automático, o ventilador fica desligado

  // ---- saídas ----
  rele(luz);
  motor(COM_MOTOR && ventilador);

  // ---- entrega o estado para a tarefa de rede ----
  gTemp = temp; gPressao = pressao; gDist = dist; gLdr = ldr;
  gPresenca = presenca; gLuz = luz; gVent = ventilador; gManual = manual;

  // ---- tela e serial ----
  if (agora - tTela >= 500) {
    tTela = agora;
    bool wifiOk = WiFi.status() == WL_CONNECTED;
    oled.clearDisplay(); oled.setTextSize(1);
    oled.setCursor(0, 0);  if (sensorOk) oled.printf("T:%.1fC P:%.0fhPa", temp, pressao); else oled.print("BMP180: sem resposta");
    oled.setCursor(0, 8);  oled.printf("LDR:%d %s", ldr, escuro ? "ESCURO" : "CLARO");
    oled.setCursor(0, 16); oled.printf("Dist:%.1fcm %s", dist, presenca ? "PRESENTE" : "");
    oled.setCursor(0, 24); oled.printf("Modo:%s%s", manual ? "MANUAL" : "AUTO", (manual && !trava) ? " (app)" : "");
    oled.setCursor(0, 32); oled.printf("Luz:%s  Fan:%s", luz ? "ON" : "OFF", COM_MOTOR ? (ventilador ? "ON" : "OFF") : "n/d");
    oled.setCursor(0, 40); oled.printf("push=%d trava=%d", !digitalRead(PIN_PUSH), trava);
    oled.setCursor(0, 48); oled.printf("WiFi:%s MQTT:%s", wifiOk ? "OK" : "--", gMqttOk ? "OK" : "--");
    oled.setCursor(0, 56); if (wifiOk) oled.print(WiFi.localIP()); else oled.print("sem rede");
    oled.display();
    Serial.printf("T=%.1f P=%.0f LDR=%d dist=%.1f modo=%s luz=%d fan=%d wifi=%d mqtt=%d\n",
                  temp, pressao, ldr, dist, manual ? "MANUAL" : "AUTO", luz, ventilador, wifiOk, (int)gMqttOk);
  }
}
