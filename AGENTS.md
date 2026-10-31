# Guía para agentes (y humanos) que sigan este proyecto

Lee esto antes de cambiar algo. El README explica el uso; aquí van las reglas y trampas.

## Piezas y responsabilidades

- `hermes-hook/handler.py`: corre **dentro** del gateway de Hermes, en su event loop. Debe ser instantáneo y nunca lanzar excepciones: solo arma un dict y hace `sendto` no bloqueante. Nada de HTTP, archivos ni imports pesados. No mandar texto de mensajes (privacidad).
- `bridge/orion_cara.py`: dueño de la lógica. Decide la cara con `decidir_cara()` (prioridad: servicio caído > turno en curso/resultado reciente > LLM apagado > reposo). `Turnos` sigue sesiones por `session_id`; hay turnos que terminan sin `agent:end`, por eso existe `SIN_RESPUESTA_TRAS`.
- `tools/avatar.sh` / `tools/subir.sh`: toda la preparación de imágenes ocurre en el PC (ffmpeg + ImageMagick). La placa solo decodifica.
- Firmware (`src/`): tonto a propósito. Solo dibuja lo que le dicen por serie. La lógica nueva va en el puente, no en el ESP32.

## Reglas de pantalla (evitan el parpadeo)

- Nunca `fillScreen`/`fillRect` grande en respuesta a comandos frecuentes. La barra se redibuja con `setTextPadding` (`dibujarTexto`, `dibujarEstado`); solo `dibujarBarra()` la borra, y solo al iniciar o tras un clip.
- El puente manda cada comando solo si su valor cambió (`mostrar()`), y redondea CPU/GPU a pasos de 5 %.
- La cara se dibuja en un sprite en PSRAM y se empuja entero: no dibujar directo en `tft` dentro de `Cara`.

## Avatares: reglas que no se ven a simple vista

- Archivos planos `<avatar>@<estado>.mjpeg` en la raíz de FFat: `mkfatfs` falla con subcarpetas ("failed to open ... for writing").
- JPEG siempre `yuvj420p`. Con `yuvj444p` la placa dibuja bloques de basura (parece otro archivo, no lo es).
- El fondo transparente se codifica como magenta (color clave). `esClave()` en `clips.cpp` tiene tolerancia por los artefactos JPEG; no usar personajes muy magenta.
- Nombres de avatar: `[a-z0-9_-]` (sin `@` ni espacios).
- Estados del firmware ↔ archivo: `choro` → `reposo`; el resto igual. Si falta un estado, la placa usa `reposo`.
- Preferencias persistidas en NVS (`Preferences`, espacio `orion`): `avatar`, `fondo`, `fondo_hex`, `anillo`, `circulo`, `vertical`.

## Flujo de trabajo

```bash
systemctl --user stop orion-cara            # libera /dev/ttyUSB0
pio run -t upload                            # firmware
tools/subir.sh <avatares...>                 # convierte y sube (borra lo que había en la flash)
systemctl --user start orion-cara
python3 bridge/orion_cara.py simular ok      # prueba sin WhatsApp
```

- Cambios en `hermes-hook/` requieren `systemctl --user restart hermes-gateway`. Antes, verificar que el LLM no esté procesando (`curl -s localhost:8081/slots`), para no cortar una respuesta.
- `uploadfs` reemplaza toda la partición: los clips que no estén en `data/` se pierden.
- Al abrir el puerto serie, la placa puede reiniciarse (DTR/RTS); el puente espera 2.5 s.

## No hacer

- No subir clips `.mjpeg` ni GIFs de avatares al repo (derechos de autor y tamaño); `.gitignore` excluye `data/**/*.mjpeg`.
- No olvidar detener el puente antes de `pio run -t upload`: si el puerto está ocupado la subida falla y la placa queda con el firmware viejo.
- No poner datos personales, tokens ni IDs de cuentas en el repo (es público).
- No cambiar los pines: son los de la Lily Pi (`Setup52_LilyPi_ST7796.h` de TFT_eSPI).
