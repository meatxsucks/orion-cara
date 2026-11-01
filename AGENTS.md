# Guía para agentes (y humanos) que sigan este proyecto

Lee esto antes de cambiar algo. El README explica el uso; aquí van las reglas y trampas.

## Piezas y responsabilidades

- `hermes-hook/handler.py`: corre **dentro** del gateway de Hermes, en su event loop. Debe ser instantáneo y nunca lanzar excepciones: solo arma un dict y hace `sendto` no bloqueante. Nada de HTTP, archivos ni imports pesados. No mandar texto de mensajes (privacidad).
- `bridge/orion_cara.py`: dueño de la lógica. Decide el estado con `decidir_cara()` (prioridad: servicio caído > turno en curso/resultado reciente > LLM o Hermes apagado > reposo) y solo manda **estados**, no frases. `Turnos` sigue sesiones por `session_id`; hay turnos que terminan sin `agent:end`, por eso un turno se da por perdido si el LLM lleva `SIN_ACTIVIDAD_TRAS` (120 s) sin trabajar según `/slots`, o a los `TURNO_MAX` (20 min). Nunca usar un timeout fijo corto: con contexto largo Orión tarda más de 5 min y la pantalla mentía "no respondió".
- Conexión: `abrir_conexion()` usa USB si hay un CP210x, si no TCP a `ORION_CARA_HOST:7777` con `token`. `Serie` y `Red` exponen `enviar()` y `drenar()`.
- `tools/avatar.sh` / `tools/subir.sh`: toda la preparación de imágenes ocurre en el PC (ffmpeg + ImageMagick). La placa solo decodifica.
- Firmware (`src/`): tonto a propósito. Solo dibuja lo que le dicen por USB o WiFi. La lógica nueva va en el puente, no en el ESP32. Excepción: las frases del recuadro viven con el avatar (`<avatar>@frases.txt`), porque el avatar se cambia tocando la placa y el puente no se entera.
- `src/red.*`: WiFi + servidor TCP (un cliente a la vez; uno nuevo reemplaza al anterior) + mDNS `orion-cara.local`. Credenciales y token en NVS, espacio `wifi`; se graban solo por USB (`tools/wifi.sh`). `wifi …` por TCP se rechaza.
- `src/panel.*`: recuadro y línea de estado.

## Reglas de pantalla (evitan el parpadeo)

- Nunca `fillScreen`/`fillRect` grande en respuesta a comandos frecuentes. El recuadro y el estado se dibujan completos en sprites (`panel.cpp`) y se empujan de una vez; `panel::caja()` y `panel::estado()` ignoran textos repetidos.
- No volver a `drawString` con `setTextPadding` directo en `tft` cambiando de fuente: el relleno solo cubre la altura de la fuente nueva y quedan restos de la anterior (se veía un rectángulo con letra chica encima).
- Texto con fuentes suaves de `fuentes.h` (UTF-8, con tildes). Para otro tamaño o fuente, editar `FUENTES` en `tools/fuente.py` y regenerar; los caracteres que no estén en `CARACTERES` salen como un rectángulo.
- El puente manda cada comando solo si su valor cambió (`mostrar()`), redondea CPU/GPU a pasos de 5 % y cada `PING_CADA` (10 s) reenvía `demo off`, `cara` y `estado`. `cara` con el mismo estado no hace nada en la placa, así que el reenvío no parpadea.
- La cara se dibuja en un sprite en PSRAM y se empuja entero: no dibujar directo en `tft` dentro de `Cara`.
- Comparaciones de tiempo en el `loop()`: tomar `millis()` **después** de procesar los comandos. Si `ahora` es anterior a `ultimoComando`, `ahora - ultimoComando` (unsigned) da un número gigante y la placa cae en demo en cada comando.

## Avatares: reglas que no se ven a simple vista

- Archivos planos `<avatar>@<estado>.mjpeg` en la raíz de FFat: `mkfatfs` falla con subcarpetas ("failed to open ... for writing").
- JPEG siempre `yuvj420p`. Con `yuvj444p` la placa dibuja bloques de basura (parece otro archivo, no lo es).
- El fondo transparente se codifica como magenta (color clave). `esClave()` en `clips.cpp` tiene tolerancia por los artefactos JPEG; no usar personajes muy magenta.
- Nombres de avatar: `[a-z0-9_-]` (sin `@` ni espacios).
- Estados del firmware ↔ archivo: `choro` → `reposo`; el resto igual. Si falta un estado, la placa usa `reposo`.
- Frases: `~/esp32/avatares/<nombre>/frases.txt` (`estado: frase`) → `data/<nombre>@frases.txt`. Sin archivo, frases neutras de `FRASES_NEUTRAS` en `main.cpp`. Sin modismos ("po") en las neutras.
- Preferencias persistidas en NVS (`Preferences`, espacio `orion`): `avatar`, `fondo`, `fondo_hex`, `anillo`, `recuadro`, `borde`, `letras`, `circulo`, `vertical`.

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
- Al abrir el puerto serie, la placa puede reiniciarse (DTR/RTS); el puente espera 2.5 s y reenvía todo en el siguiente ping. La placa arranca en demo: sin `demo off` se queda ahí.
- Las respuestas de la placa que no son `ok` salen en `journalctl --user -u orion-cara` como `placa: …`.

## No hacer

- No subir clips `.mjpeg` ni GIFs de avatares al repo (derechos de autor y tamaño); `.gitignore` excluye `data/**/*.mjpeg`.
- No olvidar detener el puente antes de `pio run -t upload`: si el puerto está ocupado la subida falla y la placa queda con el firmware viejo.
- No poner datos personales, tokens ni IDs de cuentas en el repo (es público). La clave del WiFi y el token nunca pasan por argumentos de línea de comandos ni por el chat: `wifi.sh` los lee con `read -s` y los pasa por stdin.
- No cambiar los pines: son los de la Lily Pi (`Setup52_LilyPi_ST7796.h` de TFT_eSPI).
