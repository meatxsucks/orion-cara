# Guía para agentes (y humanos) que sigan este proyecto

Lee esto antes de cambiar algo. El README explica el uso; aquí van las reglas y trampas.

## Piezas y responsabilidades

- `hermes-hook/handler.py`: corre **dentro** del gateway de Hermes, en su event loop. Debe ser instantáneo y nunca lanzar excepciones: solo arma un dict y hace `sendto` no bloqueante. Nada de HTTP, archivos ni imports pesados. No mandar texto de mensajes (privacidad).
- `bridge/orion_cara.py`: dueño de la lógica. Decide la cara con `decidir_cara()` (prioridad: servicio caído > turno en curso/resultado reciente > LLM apagado > reposo). `Turnos` sigue sesiones por `session_id`; hay turnos que terminan sin `agent:end`, por eso existe `SIN_RESPUESTA_TRAS`.
- Firmware (`src/`): tonto a propósito. Solo dibuja lo que le dicen por serie. La lógica nueva va en el puente, no en el ESP32.

## Reglas de pantalla (evitan el parpadeo)

- Nunca `fillScreen`/`fillRect` grande en respuesta a comandos frecuentes. La barra se redibuja con `setTextPadding` (`dibujarTexto`, `dibujarEstado`); solo `dibujarBarra()` la borra, y solo al iniciar o tras un clip.
- El puente manda cada comando solo si su valor cambió (`mostrar()`), y redondea CPU/GPU a pasos de 5 %.
- La cara se dibuja en un sprite en PSRAM y se empuja entero: no dibujar directo en `tft` dentro de `Cara`.

## Flujo de trabajo

```bash
systemctl --user stop orion-cara            # libera /dev/ttyUSB0
pio run -t upload                            # firmware
pio run -t uploadfs                          # data/ (borra lo que había en la flash)
systemctl --user start orion-cara
python3 bridge/orion_cara.py simular ok      # prueba sin WhatsApp
```

- Cambios en `hermes-hook/` requieren `systemctl --user restart hermes-gateway`. Antes, verificar que el LLM no esté procesando (`curl -s localhost:8081/slots`), para no cortar una respuesta.
- `uploadfs` reemplaza toda la partición: los clips que no estén en `data/` se pierden.
- Al abrir el puerto serie, la placa puede reiniciarse (DTR/RTS); el puente espera 2.5 s.

## No hacer

- No subir clips `.mjpeg` al repo (derechos de autor y tamaño); `.gitignore` ya los excluye salvo `demo.mjpeg`.
- No poner datos personales, tokens ni IDs de cuentas en el repo (es público).
- No cambiar los pines: son los de la Lily Pi (`Setup52_LilyPi_ST7796.h` de TFT_eSPI).
