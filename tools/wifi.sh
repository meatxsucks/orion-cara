#!/usr/bin/env bash
# Configura el WiFi de la placa por USB: pide red y clave (sin mostrarla), genera un token
# y lo guarda en ~/.config/orion-cara/env para que el puente pueda conectarse por la red.
set -euo pipefail

RAIZ="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="$HOME/.config/orion-cara/env"

read -rp "Nombre de la red WiFi (2,4 GHz): " SSID
read -rsp "Clave: " CLAVE
echo
[ -n "$SSID" ] || { echo "Falta el nombre de la red"; exit 1; }

mkdir -p "$(dirname "$CONFIG")"
touch "$CONFIG"
chmod 600 "$CONFIG"
TOKEN="$(grep -s '^ORION_CARA_TOKEN=' "$CONFIG" | cut -d= -f2- || true)"
if [ -z "$TOKEN" ]; then
  TOKEN="$(python3 -c 'import secrets; print(secrets.token_hex(16))')"
  echo "ORION_CARA_TOKEN=$TOKEN" >> "$CONFIG"
fi

systemctl --user stop orion-cara 2>/dev/null || true
trap 'systemctl --user start orion-cara 2>/dev/null || true' EXIT

GRABAR=$(cat <<'PY'
import glob, sys, time
import serial

ssid, clave, token = (sys.stdin.readline().rstrip("\n") for _ in range(3))
puertos = glob.glob("/dev/serial/by-id/*CP210*") + glob.glob("/dev/ttyUSB*")
if not puertos:
    sys.exit("No encuentro la placa por USB")
s = serial.Serial()
s.port, s.baudrate, s.timeout = puertos[0], 115200, 0.3
s.dtr = s.rts = False
s.open()
time.sleep(2.5)
s.reset_input_buffer()
for linea in (f"wifi ssid {ssid}", f"wifi clave {clave}", f"wifi token {token}", "wifi conectar"):
    s.write((linea + "\n").encode())
    time.sleep(0.3)
fin = time.time() + 25
while time.time() < fin:
    linea = s.readline().decode(errors="replace").strip()
    if linea.startswith("error"):
        sys.exit(f"La placa respondió: {linea} (¿firmware sin WiFi? corre: pio run -t upload)")
    if linea.startswith("wifi conectado"):
        print(f"Listo: {linea}")
        sys.exit(0)
sys.exit("No se conectó en 25 s: revisa nombre y clave, y que la red sea de 2,4 GHz")
PY
)
printf '%s\n%s\n%s\n' "$SSID" "$CLAVE" "$TOKEN" | python3 -I -c "$GRABAR"
