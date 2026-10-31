# orion-cara

Una pantallita de escritorio con un **avatar animado** que muestra qué está haciendo tu asistente de IA local: piensa mientras procesa tu mensaje de WhatsApp, se alegra cuando responde, pone cara de error si no responde y se duerme cuando el LLM está apagado. Abajo muestra el estado del PC (LLM, gateway, webhook, CPU, RAM y GPU).

El avatar se elige: Pokémon, Cthulhu, Goku o lo que quieras, dentro de un **círculo de color** que también se cambia.

Hecho para una **LilyGO Lily Pi** (ESP32 con pantalla táctil de 3.5") y un asistente con [Hermes Agent](https://github.com/NousResearch/hermes-agent) + llama.cpp.

```
 WhatsApp ─► Meta Cloud API ─► Hermes gateway ─hook orion_cara─► socket unix ─► bridge (PC) ─USB─► Lily Pi
                                   │                                               ▲
                                   └─► llama.cpp ◄── estado: systemd, /slots, CPU/RAM/GPU ──┘
```

## Inicio rápido

Requisitos en el PC: [PlatformIO](https://platformio.org/), `ffmpeg`, ImageMagick, Python 3 con `pyserial` y `psutil` (`nvidia-smi` opcional).
En Arch: `pacman -S platformio-core platformio-core-udev ffmpeg imagemagick python-pyserial python-psutil`. Tu usuario debe tener acceso al puerto serie (grupo `uucp` en Arch, `dialout` en Debian/Ubuntu).

```bash
# 1. firmware
pio run -t upload

# 2. avatares (ver "Crear un avatar")
tools/subir.sh pikachu cthulhu

# 3. puente en el PC
ln -s "$PWD/bridge/orion-cara.service" ~/.config/systemd/user/
systemctl --user daemon-reload && systemctl --user enable --now orion-cara

# 4. hook de Hermes (los hooks se cargan al arrancar el gateway)
ln -s "$PWD/hermes-hook" ~/.hermes/hooks/orion_cara
systemctl --user restart hermes-gateway

# 5. probar sin mandar mensajes
python3 bridge/orion_cara.py simular
```

> El servicio asume que el repo está en `~/esp32/orion-cara` (puede ser un symlink); ajusta `ExecStart` si no.

## Usar la pantalla

| Acción | Cómo |
|---|---|
| Cambiar de avatar | **tocar** la pantalla, o `python3 bridge/orion_cara.py enviar avatar pikachu` |
| Cambiar el color del círculo | **mantener presionado** (~1 s), o `enviar fondo morado` / `enviar fondo #FF8800` |
| Color del anillo | `enviar anillo blanco` |
| Sin círculo | `enviar circulo off` |
| Vertical / horizontal | `enviar girar vertical` / `enviar girar horizontal` |
| Ver avatares cargados | `enviar avatares` (la respuesta sale en `journalctl --user -u orion-cara`) |
| Mensaje en pantalla | `enviar texto Hola!` (15 s) |

Colores con nombre: `azul`, `celeste`, `morado`, `rosado`, `rojo`, `naranja`, `amarillo`, `verde`, `gris`, `negro`, `blanco`. La placa **recuerda** avatar, colores y orientación aunque la desconectes.

## Qué muestra

| Situación | Estado del avatar | Frase |
|---|---|---|
| Llega un mensaje por WhatsApp | `pensando` | `WhatsApp: pensando...` / `usando <herramienta>` |
| Orión respondió | clip `listo` + `feliz` (10 s) | `WhatsApp: respondido!` |
| Terminó sin respuesta / 5 min sin terminar | `error` (30 s) | `WhatsApp: no respondio` |
| Un servicio en estado `failed` | `error` | `Chuta, se cayo un servicio` |
| LLM apagado | `durmiendo` (al prender: clip `despierta`) | `Modo juego: LLM apagado` |
| Nada en curso | `reposo` | `Wena, soy Orion` |

## Crear un avatar

Cada avatar es una carpeta en el PC, por defecto `~/esp32/avatares/<nombre>/` (cambiable con `AVATARES_DIR`), con **un archivo por estado**:

```
~/esp32/avatares/pikachu/
  reposo.pixel.gif        ← obligatorio
  feliz.pixel.gif         ← opcional
  ...
```

- **Estados:** `reposo` (obligatorio), `pensando`, `feliz`, `durmiendo`, `error`, `listo`, `despierta`. Los que falten **se generan solos** desde `reposo` con efectos: `?` al pensar, salto al estar feliz, `z` y oscuro al dormir, rojo y temblor en error, fundido al despertar.
- **Modo** (parte del nombre, opcional), según el tipo de imagen:

| Modo | Para | Ejemplo |
|---|---|---|
| `pixel` | sprites con transparencia en pixel art (sin suavizar) | sprites animados de Pokémon Showdown |
| `blanco` | stickers/emojis con fondo blanco: quita el fondo que toca los bordes (no el blanco de los ojos) | emojis animados |
| `foto` | videos/imágenes con fondo: llenan todo el círculo | escenas, GIFs de anime |
| *(ninguno)* | imágenes con transparencia, escalado suave | stickers PNG/GIF transparentes |

Luego:

```bash
tools/subir.sh pikachu cthulhu charmander   # convierte y sube SOLO esos (reemplaza lo que había)
tools/subir.sh --todos                       # todos los de ~/esp32/avatares
tools/avatar.sh ~/esp32/avatares/pikachu     # solo convertir uno, sin subir
```

Opciones de `avatar.sh` por variable: `SEGUNDOS` (2; se redondea a ciclos completos del GIF), `MAX_SEGUNDOS` (4), `FPS` (10), `CALIDAD` (10; 2 = mejor, 31 = peor).

### Espacio

La flash tiene **~9.9 MB** para avatares y cada uno ocupa **~1–1.3 MB**: caben 5–8 a la vez. Ten todos los que quieras en el PC y sube solo los que vayas a usar; `subir.sh` avisa si no caben.

> Los avatares no se suben al repo (`.gitignore`). Si usas personajes con derechos de autor, quedan solo en tu PC y tu placa.

## Hardware

| | |
|---|---|
| Placa | LilyGO Lily Pi (ESP32-WROVER, 16 MB flash, 8 MB PSRAM) |
| Pantalla | ST7796 3.5" 480×320, SPI: MOSI 19, MISO 23, SCLK 18, CS 5, DC 27, BL 12 |
| Táctil | FT6236 en I2C 0x38 (SDA 21, SCL 22) |
| Otros | RTC PCF8563 (0x51), relé, microSD. **Sin giroscopio**: la orientación se elige con `girar` |

Conectores:
- **USB-C del lado del CP2104**: energía **y** datos. Es el que va al PC (el puente habla por ahí).
- **El otro USB-C y el USB-A**: son pines del ESP32, **no** USB reales (no sirven para pendrives ni datos).
- **Jack DC 5–12 V**: solo energía.

## Estructura

```
src/            firmware (PlatformIO + Arduino)
  main.cpp      bucle principal, barra, táctil, avatares, comandos por USB
  clips.*       reproductor MJPEG con círculo y color clave
  cara.*        cara dibujada por código (si no hay avatares cargados)
tools/
  avatar.sh     carpeta de GIFs → avatar (7 estados .mjpeg)
  subir.sh      elige avatares y los sube a la flash
  clip.sh       convierte un video suelto a .mjpeg
bridge/         puente en el PC (Python) + servicio systemd de usuario
hermes-hook/    hook de gateway de Hermes que avisa los turnos
```

## Puente y hook

`bridge/orion_cara.py` revisa cada 2 s los servicios systemd de usuario del LLM y del gateway, `/slots` de llama.cpp, CPU/RAM/GPU y si el webhook escucha, y recibe del hook los eventos de cada turno. Se reconecta solo si desconectas la placa.

```bash
python3 bridge/orion_cara.py estado                      # estado leído, sin pantalla
python3 bridge/orion_cara.py simular [ok|sin_respuesta]  # turno de WhatsApp falso
python3 bridge/orion_cara.py enviar <comando>            # cualquier comando del protocolo
journalctl --user -u orion-cara -f                       # eventos que llegan
```

El hook (`hermes-hook/`) escucha `agent:start`, `agent:step` y `agent:end`, filtra por canal (`ORION_CARA_CANALES`, por defecto `whatsapp_cloud,whatsapp`) y manda **solo metadatos** al puente por `$XDG_RUNTIME_DIR/orion-cara.sock`: evento, canal, paso, herramientas y si hubo respuesta. Nunca el texto de los mensajes. Es no bloqueante: si el puente no corre, no pasa nada.

Variables del puente: `ORION_CARA_PUERTO`, `ORION_LLM_URL` (`http://127.0.0.1:8081`), `ORION_SERVICIO_LLM`, `ORION_SERVICIO_HERMES`, `ORION_PUERTO_WEBHOOK` (8090).

## Protocolo serie (115200 baudios)

Una línea por comando:

| Comando | Efecto |
|---|---|
| `cara <reposo\|choro\|feliz\|pensando\|durmiendo\|error>` | cambia el estado |
| `clip <nombre>` | reproduce un clip del avatar (`listo`, `despierta`…) o uno suelto |
| `texto <mensaje>` / `estado <mensaje>` | línea grande / línea chica de la barra |
| `avatar <nombre>` / `avatares` | elige avatar / lista los cargados |
| `fondo <color>` / `anillo <color>` / `circulo on\|off` | estilo del círculo |
| `girar [vertical\|horizontal]` | orientación |
| `demo on\|off` | modo demo (sin PC vuelve solo a demo tras 30 s) |

## Detalles técnicos

- Los avatares se guardan como archivos planos `<avatar>@<estado>.mjpeg` en una partición FAT (`app3M_fat9M_16MB.csv`): la herramienta `mkfatfs` no crea carpetas.
- Cada cuadro es un JPEG **4:2:0** de 320×320; con 4:4:4 el decodificador (JPEGDEC) muestra basura.
- El fondo del avatar se codifica en **magenta**; al decodificar, la placa lo reemplaza por el color del círculo y pinta el anillo y el exterior. Por eso cambiar el fondo es instantáneo y no requiere reconvertir.
- ~10 fps a 320×320 en un ESP32 clásico.

## Ideas

- **WiFi** en vez de USB para el estado, para alimentar la placa con cualquier cargador.
- **microSD** para tener decenas de avatares sin reemplazar.

## Referencias

- [Hermes Agent: Event Hooks](https://github.com/NousResearch/hermes-agent) (`website/docs/user-guide/features/hooks.md`)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) y [JPEGDEC](https://github.com/bitbank2/JPEGDEC)
- [Pokémon Showdown sprites](https://play.pokemonshowdown.com/sprites/) (sprites animados, no incluidos)
- [xdwarf/deskpet](https://github.com/xdwarf/deskpet), [arvindelavari/mjpeg-decoder](https://github.com/arvindelavari/mjpeg-decoder)

## Licencia

MIT
