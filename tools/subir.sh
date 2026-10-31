#!/usr/bin/env bash
# Sube a la placa solo los avatares elegidos (reemplaza todo lo que había en la flash).
# Uso: tools/subir.sh <avatar> [avatar...]      ej: tools/subir.sh cthulhu pikachu
#      tools/subir.sh --todos
# Lee cada avatar desde AVATARES_DIR (por defecto ~/esp32/avatares/<nombre>/).
set -euo pipefail

raiz="$(cd "$(dirname "$0")/.." && pwd)"
origen="${AVATARES_DIR:-$HOME/esp32/avatares}"

if [[ $# -lt 1 ]]; then
  sed -n '2,5p' "$0" >&2
  echo "Disponibles en $origen: $(ls "$origen" | tr '\n' ' ')" >&2
  exit 1
fi
[[ "$1" == --todos ]] && set -- $(ls "$origen")

rm -rf "$raiz/data/avatares" "$raiz/data/"*@*.mjpeg
for a in "$@"; do
  [[ -d "$origen/$a" ]] || { echo "No existe $origen/$a" >&2; exit 1; }
  "$raiz/tools/avatar.sh" "$origen/$a" "$a" | tail -2 | head -1
done

total=$(du -sk "$raiz/data" | cut -f1)
if (( total > 10000 )); then
  echo "No cabe: ${total} KB de ~10100 KB. Elige menos avatares." >&2
  exit 1
fi

cd "$raiz"
systemctl --user stop orion-cara 2>/dev/null || true
pio run -t uploadfs | grep -E 'SUCCESS|FAILED'
systemctl --user start orion-cara 2>/dev/null || true
echo "Listo. Cambia de avatar tocando la pantalla o con: python3 bridge/orion_cara.py enviar avatar <nombre>"
