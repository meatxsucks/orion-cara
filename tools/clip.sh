#!/usr/bin/env bash
# Convierte un video o GIF a MJPEG 240x240 listo para la pantalla y lo deja en data/.
# Uso: tools/clip.sh <entrada> [nombre] [segundos] [fps]
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Uso: $0 <video|gif> [nombre] [segundos=8] [fps=12]" >&2
  exit 1
fi

entrada="$1"
nombre="${2:-$(basename "${entrada%.*}")}"
segundos="${3:-8}"
fps="${4:-12}"
dir="$(cd "$(dirname "$0")/.." && pwd)/data"
salida="$dir/${nombre}.mjpeg"

ffmpeg -hide_banner -loglevel error -y -t "$segundos" -i "$entrada" \
  -vf "fps=${fps},scale=240:240:force_original_aspect_ratio=increase,crop=240:240" \
  -pix_fmt yuvj420p -q:v 8 -an -f mjpeg "$salida"

tam=$(du -k "$salida" | cut -f1)
total=$(du -ck "$dir"/*.mjpeg | tail -1 | cut -f1)
echo "OK: $salida (${tam} KB). Total en data/: ${total} KB de ~10100 KB."
echo "Sube los clips con: pio run -t uploadfs"
