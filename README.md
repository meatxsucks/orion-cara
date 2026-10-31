# orion-cara

Cara animada y pantalla de estado para un asistente de IA local, en una **LilyGO Lily Pi** (ESP32 con pantalla táctil de 3.5").

La pantalla muestra una cara que cambia de expresión según lo que hace la IA: duerme cuando el LLM está apagado, piensa mientras genera una respuesta, se alegra cuando termina y pone cara de error si se cae un servicio. Abajo muestra el estado del PC (LLM, gateway, CPU, RAM y hora).

Cada expresión puede ser la cara dibujada por código o un **clip en loop** (skin) con el personaje que quieras.

## Hardware

| | |
|---|---|
| Placa | LilyGO Lily Pi (ESP32-WROVER, 16 MB flash, 8 MB PSRAM) |
| Pantalla | ST7796 3.5" 480×320, SPI: MOSI 19, MISO 23, SCLK 18, CS 5, DC 27, BL 12 |
| Táctil | FT6236 en I2C 0x38 (SDA 21, SCL 22) |
| USB-serie | CP2104 |

No hace falta tarjeta SD: los clips van en una partición FAT de ~9.9 MB en la flash interna.

## Estructura

```
src/            firmware (PlatformIO + Arduino)
  main.cpp      bucle principal, táctil, comandos por USB
  cara.*        cara animada dibujada en un sprite
  clips.*       reproductor MJPEG (clips sueltos y skins en loop)
data/           clips .mjpeg que se suben a la flash
tools/clip.sh   convierte video/GIF a MJPEG 240×240
bridge/         puente en el PC (Python) + servicio systemd de usuario
```

## Instalación

Requisitos en el PC: [PlatformIO](https://platformio.org/), `ffmpeg`, Python 3 con `pyserial` y `psutil`. En Arch: `pacman -S platformio-core platformio-core-udev ffmpeg python-pyserial python-psutil`.
Tu usuario necesita acceso al puerto serie (`uucp` en Arch, `dialout` en Debian/Ubuntu).

```bash
pio run -t upload      # firmware
pio run -t uploadfs    # clips de data/
```

> Si el puente está corriendo, ocupa el puerto: detenlo antes de subir (`systemctl --user stop orion-cara`).

## Clips y skins

```bash
tools/clip.sh mi_video.mp4 nombre [segundos=8] [fps=12]
pio run -t uploadfs
```

- `data/<nombre>.mjpeg`: clip suelto, se reproduce en modo demo o con el comando `clip <nombre>`.
- `data/cara_<expresion>.mjpeg`: **skin** de esa expresión, se repite en loop en lugar de la cara dibujada. Expresiones: `choro`, `feliz`, `pensando`, `durmiendo`, `error`.
- Clips de eventos que lanza el puente si existen: `despierta` (se prende el LLM), `listo` (terminó de responder), `error`.

Ejemplo con un personaje: `tools/clip.sh personaje_meditando.gif cara_pensando 4`.

Rendimiento medido: ~12 fps a 240×240 con un ESP32 clásico. Un segundo de clip ocupa ~80–100 KB.

> Los clips no se suben al repo (`.gitignore`). Si usas personajes con derechos de autor, quedan solo en tu placa.

## Puente con la IA (PC)

`bridge/orion_cara.py` revisa cada 2 s:

- los servicios systemd de usuario del LLM y del gateway (`llm.service`, `hermes-gateway.service`),
- `/slots` de llama.cpp para saber si está generando,
- CPU y RAM,

y manda la expresión y el estado por USB. Se reconecta solo si desconectas la placa.

```bash
python3 bridge/orion_cara.py estado                   # ver el estado sin pantalla
python3 bridge/orion_cara.py enviar texto Hola!       # mostrar un mensaje (15 s)
python3 bridge/orion_cara.py enviar cara feliz
python3 bridge/orion_cara.py enviar clip listo

ln -s "$PWD/bridge/orion-cara.service" ~/.config/systemd/user/
systemctl --user enable --now orion-cara
```

El servicio asume que el repo está en `~/esp32/orion-cara`; ajusta `ExecStart` si no.

Variables de entorno: `ORION_CARA_PUERTO`, `ORION_LLM_URL` (por defecto `http://127.0.0.1:8081`), `ORION_SERVICIO_LLM`, `ORION_SERVICIO_HERMES`.

## Protocolo serie (115200 baudios)

Una línea por comando:

| Comando | Efecto |
|---|---|
| `cara <expresion>` | cambia la expresión |
| `clip <nombre>` | reproduce un clip suelto |
| `texto <mensaje>` | frase grande de la barra |
| `estado <mensaje>` | línea chica de estado |
| `demo on\|off` | modo demo (expresiones y clips al azar) |
| `lista` | lista los clips en la flash |

Sin comandos durante 30 s, la placa vuelve sola al modo demo. Tocar la pantalla pasa a la siguiente expresión.

## Referencias

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (ejemplos `Animated_Eyes`) y [JPEGDEC](https://github.com/bitbank2/JPEGDEC)
- [xdwarf/deskpet](https://github.com/xdwarf/deskpet): sistema de expresiones de una mascota de escritorio
- [arvindelavari/mjpeg-decoder](https://github.com/arvindelavari/mjpeg-decoder) y [ifeech/esp_looping_video_player](https://github.com/ifeech/esp_looping_video_player): reproducción MJPEG en ESP32

## Licencia

MIT
