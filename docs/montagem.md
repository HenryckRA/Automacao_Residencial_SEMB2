# Notas de montagem

Registro do que aprendemos montando a bancada. Serve de checklist para a placa final.

## Estágio do motor (ventilador)

- Fonte externa de 5 V. O positivo vai para o trilho de +5 V, o motor fica entre o +5 V e o coletor do TIP122, e o emissor vai para o GND.
- O GND da fonte e o GND do ESP32 precisam ser o mesmo. Sem GND comum o transistor não conduz.
- Diodo 1N4007 em paralelo com o motor, com a faixa do lado do +5 V.
- A base do TIP122 vem do GPIO13 por um resistor de 1 kΩ. Nenhum resistor em série com o motor.
- Nunca alimentar o motor pelo pino de 5 V do ESP32.

## Protoboard

- Em algumas protoboards os trilhos de alimentação são divididos ao meio. Antes de montar, teste a continuidade do trilho inteiro (por exemplo, com um LED e um resistor).
- O que deixou o motor parado na primeira montagem: diodo invertido, fonte ligada na metade morta do trilho, GND do ESP32 isolado em outro lado da placa e motor ligado entre GND e coletor, em vez de entre +5 V e coletor. Se acontecer de novo, confira nessa ordem.

## Sensor de temperatura

- O AHT10 deu leituras instáveis por solda ruim. Com a biblioteca Adafruit_AHTX0, uma falha de leitura bloqueia o `loop()` por cerca de 1 s, e o botão perde cliques. O teste com o AHT10 usa um driver próprio sem bloqueio.
- O BMP180 (GY-68, endereço 0x77) mede temperatura e pressão, não umidade.
- Teste de puxão leve em cada fio e pino soldado antes de ir para a placa final.

## Botões

- Botão push e chave com trava usam `INPUT_PULLUP`, com o outro lado no GND.
- Contato intermitente no push: soldar fios nas pernas diagonais costuma resolver.

## HC-SR04

- O ECHO sai em 5 V e o ESP32 aceita 3,3 V. O divisor de 1 kΩ + 2 kΩ no ECHO reduz a tensão para uma faixa segura (cerca de 3,3 V).
