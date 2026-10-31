#!/usr/bin/env bash
# Convierte un video o GIF a MJPEG 240x240 listo para la pantalla y lo deja en data/.
# Uso: tools/clip.sh <entrada> [nombre] [segundos] [fps]
# Variables opcionales:
#   QUITAR_BLANCO=1   reemplaza el fondo blanco por el color de fondo de la pantalla
#   ENCAJAR=1         encaja la imagen completa (con bordes) en vez de recortar al centro
#   EXTRA_VF="..."    filtros ffmpeg extra al final (p. ej. "hue=h=-90")
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
fondo="0x181C18"

if [[ "${ENCAJAR:-0}" == 1 ]]; then
  escala="scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:color=${fondo}"
else
  escala="scale=240:240:force_original_aspect_ratio=increase,crop=240:240"
fi

# Rellena solo el fondo blanco conectado a los bordes, para no borrar el blanco de los ojos.
if [[ "${QUITAR_BLANCO:-0}" == 1 ]]; then
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  magick "$entrada" -coalesce -bordercolor white -border 1 -fuzz 12% \
    -fill "#${fondo#0x}" -draw "color 0,0 floodfill" -shave 1x1 "$tmp/sin_fondo.gif"
  entrada="$tmp/sin_fondo.gif"
fi
filtro="[0:v]fps=${fps},${escala}"
[[ -n "${EXTRA_VF:-}" ]] && filtro+=",${EXTRA_VF}"

ffmpeg -hide_banner -loglevel error -y -t "$segundos" -i "$entrada" \
  -filter_complex "${filtro},format=yuvj420p" -q:v 8 -an -f mjpeg "$salida"

tam=$(du -k "$salida" | cut -f1)
total=$(du -ck "$dir"/*.mjpeg | tail -1 | cut -f1)
echo "OK: $salida (${tam} KB). Total en data/: ${total} KB de ~10100 KB."
echo "Sube los clips con: pio run -t uploadfs"
