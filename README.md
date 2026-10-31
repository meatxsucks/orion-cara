# orion-cara

Cara animada y pantalla de estado para un asistente de IA local, en una **LilyGO Lily Pi** (ESP32 con pantalla táctil de 3.5").

La cara reacciona a los **turnos del asistente en WhatsApp**: piensa mientras procesa tu mensaje (y muestra qué herramienta está usando), se alegra cuando responde y pone cara de error si no respondió. Cuando no hay turnos en curso, duerme si el LLM está apagado o queda en modo "choro". Abajo muestra el estado del PC: LLM, gateway, webhook de WhatsApp, CPU, RAM y GPU.

Cada expresión puede ser la cara dibujada por código o un **clip en loop** (skin) con el personaje que quieras.

```
 WhatsApp ──► Meta Cloud API ──► Hermes gateway ──hook orion_cara──► socket unix ──► bridge (PC) ──USB──► Lily Pi
                                     │                                                   ▲
                                     └──► llama.cpp (LLM)  ◄── estado: systemd, /slots, CPU/RAM/GPU ┘
```

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
  main.cpp      bucle principal, barra, táctil, comandos por USB
  cara.*        cara animada dibujada en un sprite (PSRAM)
  clips.*       reproductor MJPEG (clips sueltos y skins en loop)
data/           clips .mjpeg que se suben a la flash
tools/clip.sh   convierte video/GIF a MJPEG 240×240
bridge/         puente en el PC (Python) + servicio systemd de usuario
hermes-hook/    hook de gateway de Hermes Agent que avisa los turnos
```

## Instalación

Requisitos en el PC: [PlatformIO](https://platformio.org/), `ffmpeg`, Python 3 con `pyserial` y `psutil`; `nvidia-smi` opcional para la GPU. En Arch: `pacman -S platformio-core platformio-core-udev ffmpeg python-pyserial python-psutil`.
Tu usuario necesita acceso al puerto serie (`uucp` en Arch, `dialout` en Debian/Ubuntu).

```bash
pio run -t upload      # firmware
pio run -t uploadfs    # clips de data/
```

> El puente ocupa el puerto serie: detenlo antes de subir (`systemctl --user stop orion-cara`).

### Puente (PC)

```bash
ln -s "$PWD/bridge/orion-cara.service" ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now orion-cara
```

El servicio asume que el repo está en `~/esp32/orion-cara`; ajusta `ExecStart` si no.

### Hook de Hermes

```bash
ln -s "$PWD/hermes-hook" ~/.hermes/hooks/orion_cara
systemctl --user restart hermes-gateway     # los hooks se cargan solo al arrancar
```

El hook escucha `agent:start`, `agent:step` y `agent:end`, filtra por canal (`ORION_CARA_CANALES`, por defecto `whatsapp_cloud,whatsapp`) y manda **solo metadatos** al puente por un socket unix (`$XDG_RUNTIME_DIR/orion-cara.sock`): evento, canal, número de paso, nombres de herramientas y si hubo respuesta. Nunca el texto de los mensajes. Es no bloqueante: si el puente no corre, no pasa nada.

Para otros canales (p. ej. Telegram), agrega `ORION_CARA_CANALES=whatsapp_cloud,telegram` al entorno del gateway.

## Qué muestra

| Situación | Cara | Frase |
|---|---|---|
| Llega un mensaje por WhatsApp | pensando | `WhatsApp: pensando...` / `usando <herramienta>` |
| Orión respondió | feliz (10 s) + clip `listo` | `WhatsApp: respondido!` |
| Terminó sin respuesta | error (30 s) | `WhatsApp: no respondio` |
| 5 min sin `agent:end` | error (30 s) | `WhatsApp: no respondio (se pego)` |
| Un servicio en estado `failed` | error | `Chuta, se cayo un servicio` |
| LLM apagado (modo juego) | durmiendo | `Modo juego: LLM apagado` |
| Nada en curso | choro | `Wena, soy Orion` |

Línea de estado: `LLM ok  Hermes ok  WA ok  CPU 25%  RAM 44%  GPU 60% 6.8/8G 59C` (`WA` = webhook local escuchando en el puerto 8090).

## Clips y skins

```bash
tools/clip.sh mi_video.mp4 nombre [segundos=8] [fps=12]
pio run -t uploadfs
```

Opciones de `clip.sh` por variable de entorno:

- `QUITAR_BLANCO=1`: rellena el fondo blanco que toca los bordes con el color de la pantalla (ideal para emojis/stickers; no borra el blanco interior como los ojos). Requiere ImageMagick.
- `ENCAJAR=1`: encaja la imagen completa en vez de recortar al centro.
- `EXTRA_VF="..."`: filtros ffmpeg extra, p. ej. `hue=h=-95` para teñir de rojo una versión "error".

Ejemplo (skin estilo emoji):

```bash
QUITAR_BLANCO=1 ENCAJAR=1 tools/clip.sh cthulhu_pensando.gif cara_pensando 8
QUITAR_BLANCO=1 ENCAJAR=1 EXTRA_VF="hue=h=-95:s=1.6" tools/clip.sh cthulhu_pensando.gif cara_error 4
```

- `data/<nombre>.mjpeg`: clip suelto (comando `clip <nombre>` o modo demo).
- `data/cara_<expresion>.mjpeg`: **skin** de esa expresión, en loop en lugar de la cara dibujada. Expresiones: `choro`, `feliz`, `pensando`, `durmiendo`, `error`.
- Clips de eventos que lanza el puente si existen: `despierta` (se prende el LLM), `listo` (respondió), `error`.

Rendimiento medido: ~12 fps a 240×240 en un ESP32 clásico; un segundo de clip ocupa ~80–100 KB.

> Los clips no se suben al repo (`.gitignore`). Si usas personajes con derechos de autor, quedan solo en tu placa.

## Comandos del puente

```bash
python3 bridge/orion_cara.py estado              # estado leído, sin pantalla
python3 bridge/orion_cara.py simular [ok|sin_respuesta]   # turno de WhatsApp falso para probar
python3 bridge/orion_cara.py enviar texto Hola!  # mensaje en pantalla (15 s)
python3 bridge/orion_cara.py enviar cara feliz
python3 bridge/orion_cara.py enviar clip listo
journalctl --user -u orion-cara -f               # ver eventos que llegan del hook
```

Variables de entorno: `ORION_CARA_PUERTO`, `ORION_LLM_URL` (por defecto `http://127.0.0.1:8081`), `ORION_SERVICIO_LLM`, `ORION_SERVICIO_HERMES`, `ORION_PUERTO_WEBHOOK` (8090).

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

- [Hermes Agent: Event Hooks](https://github.com/NousResearch/hermes-agent) (`website/docs/user-guide/features/hooks.md`)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (ejemplos `Animated_Eyes`) y [JPEGDEC](https://github.com/bitbank2/JPEGDEC)
- [xdwarf/deskpet](https://github.com/xdwarf/deskpet): sistema de expresiones de una mascota de escritorio
- [arvindelavari/mjpeg-decoder](https://github.com/arvindelavari/mjpeg-decoder) y [ifeech/esp_looping_video_player](https://github.com/ifeech/esp_looping_video_player): reproducción MJPEG en ESP32

## Licencia

MIT
