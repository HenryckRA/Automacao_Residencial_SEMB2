# Automação Residencial SEMB2

Automação de uma sala com **ESP32**, comunicação **MQTT** (Mosquitto) e painel **Node-RED**.
Trabalho final de Sistemas Embarcados II (Engenharia Mecatrônica, UFU), em dupla:
Henrique Rodrigues Alves e Pedro Henrique Bohling Peres. Professor: Fabio Vicenzi Romualdo da Silva.

O sistema mede temperatura, luminosidade e presença e controla dois atuadores: a **iluminação** (relé) e um **ventilador** (motor DC). O painel mostra os sensores e envia comandos de volta ao ESP32.

## Como funciona

- **Luz (modo automático):** acende quando há presença (HC-SR04 a menos de 30 cm) e está escuro (LDR). Apaga 5 s depois de a presença sumir (valor dos testes; para a versão final está previsto de 30 a 60 s). O LDR usa histerese: escuro abaixo de 1000, claro acima de 1400 (leitura de 0 a 4095).
- **Ventilador (modo automático):** liga acima de 28 °C e desliga abaixo de 27 °C.
- **Modo manual:** a chave com trava (GPIO33 no GND) força o manual. O botão push liga e desliga luz e ventilador juntos. O painel também pode comandar luz, ventilador e modo pelo MQTT.
- **Display OLED:** mostra sensores, modo, saídas e o estado do Wi-Fi e do MQTT.
- **Rede sem travar o controle:** o Wi-Fi e o MQTT rodam em uma tarefa no núcleo 0 do ESP32. O controle local (botões, sensores, saídas) roda no `loop()` e não espera pela rede.

## Hardware

| Componente | Função | Pino do ESP32 |
|---|---|---|
| OLED SSD1306 128x64 (I2C, 0x3C) | Display | SDA 25, SCL 26 |
| BMP180 / GY-68 (I2C, 0x77) | Temperatura e pressão (sensor provisório) | SDA 25, SCL 26 |
| LDR + 10 kΩ (divisor) | Luminosidade | GPIO34 |
| HC-SR04 | Presença por distância | TRIG 14, ECHO 35 (divisor 1 kΩ + 2 kΩ) |
| Relé via TIP122 | Iluminação | GPIO27 (nível alto liga) |
| Motor DC via TIP122 | Ventilador | GPIO13 |
| Botão push | Alterna luz e ventilador no manual | GPIO32 (INPUT_PULLUP) |
| Chave com trava | Força o modo manual | GPIO33 (INPUT_PULLUP, no GND = manual) |

O motor usa fonte externa de 5 V com GND comum ao do ESP32. Detalhes e erros que já encontramos em [docs/montagem.md](docs/montagem.md).

## Estrutura do repositório

```
src/main.cpp                  firmware principal (PlatformIO)
include/segredos_exemplo.h    modelo das credenciais (copiar para segredos.h)
testes/                       testes de bancada, não compilados pelo PlatformIO
node-red/flow_semb2.json      fluxo do painel (importar no Node-RED)
docs/montagem.md              notas de montagem do hardware
```

## Compilar e gravar

1. Instale o [PlatformIO](https://platformio.org/) (extensão do VS Code).
2. No `platformio.ini`, a seção `lib_deps` precisa conter:

   ```ini
   lib_deps =
       adafruit/Adafruit SSD1306
       adafruit/Adafruit GFX Library
       adafruit/Adafruit BMP085 Library
       256dpi/MQTT
   ```

3. Copie `include/segredos_exemplo.h` para `include/segredos.h` e preencha o nome e a senha do Wi-Fi (2,4 GHz) e o IP do PC onde roda o Mosquitto. O `segredos.h` está no `.gitignore` e não vai para o GitHub.
4. Compile e grave pelo PlatformIO.

## MQTT

Todos os tópicos começam com `semb2/sala/`. Os payloads são texto simples.

| Tópico | Quem publica | Valor |
|---|---|---|
| `status` | ESP32 (e o broker, pelo Last Will) | `online` / `offline`, retido |
| `temperatura` | ESP32, a cada 2 s | °C |
| `pressao` | ESP32, a cada 2 s | hPa |
| `luminosidade` | ESP32, a cada 2 s | 0 a 4095 |
| `distancia` | ESP32, a cada 2 s | cm |
| `presenca` | ESP32, ao mudar | `ON` / `OFF` |
| `luz/estado`, `ventilador/estado` | ESP32, ao mudar | `ON` / `OFF`, retido |
| `modo/estado` | ESP32, ao mudar | `AUTO` / `MANUAL`, retido |
| `luz/set`, `ventilador/set` | Painel | `ON` / `OFF` |
| `modo/set` | Painel | `AUTO` / `MANUAL` |

Um comando de luz ou ventilador coloca o sistema em manual. O comando `modo/set AUTO` volta ao automático, desde que a chave física não esteja em manual.

## Painel Node-RED

1. Instale o Mosquitto (com `listener 1883` e `allow_anonymous true` no `mosquitto.conf`, para uso em rede local de testes) e o Node-RED.
2. No Node-RED, instale a paleta `@flowfuse/node-red-dashboard` (Dashboard 2.0).
3. Menu, Import, cole o conteúdo de `node-red/flow_semb2.json` e clique em Deploy.
4. O painel abre em `http://localhost:1880/dashboard/sala`.

`allow_anonymous true` serve só para a rede de testes. Em uma rede compartilhada, use usuário e senha no Mosquitto.

## Estado do projeto

Testado na bancada, com o código em `testes/`:

- [x] Display OLED, LDR, botão push, relé com LED, HC-SR04, motor com fonte externa e BMP180 (teste integrado)
- [x] Modos automático e manual no teste integrado

Testado só em simulação ou ainda não testado:

- [x] Fluxo do Node-RED e painel: testados com mensagens MQTT simuladas (Mosquitto 2.0.18, Node-RED 5.0.10, Dashboard 1.37.0)
- [ ] Firmware com MQTT (`testes/semb2_mqtt.cpp`): escrito e checado só quanto à sintaxe. Ainda não compilado de verdade nem executado no ESP32. Depois de validado, passa a ser o `src/main.cpp`
- [ ] Conexão do ESP32 com o Mosquitto pelo hotspot do celular

Pendente:

- [ ] Chave com trava (ainda não comprada) e contato intermitente do botão push
- [ ] Sensor de temperatura e umidade definitivo (AHT10); hoje o BMP180 mede temperatura e pressão, sem umidade
- [ ] Banco de dados na nuvem (serviço ainda a definir pela dupla)
- [ ] Decisão sobre voltar ao automático depois de um tempo em manual
- [ ] Placa de circuito e caixa impressa em 3D
