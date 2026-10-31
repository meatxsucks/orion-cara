#!/usr/bin/env bash
# Arma un avatar para la pantalla desde una carpeta con un archivo por estado.
# Uso: tools/avatar.sh <carpeta> [nombre]
#
# La carpeta tiene archivos <estado>[.modo].<ext>, por ejemplo "reposo.pixel.gif" o "listo.foto.mp4":
#   estados: reposo (obligatorio), pensando, feliz, durmiendo, error, listo, despierta
#   modos:   pixel  sprite con transparencia, se escala sin suavizar (pixel art)
#            blanco sticker/emoji con fondo blanco: se quita el fondo que toca los bordes
#            foto   video o imagen con fondo: llena el círculo completo
#            (sin modo) imagen con transparencia, escalado suave
# Los estados que falten se generan desde "reposo" con efectos.
# Variables: SEGUNDOS (2, mínimo; se redondea a ciclos completos del GIF), MAX_SEGUNDOS (4),
#            FPS (10), CALIDAD (10, 2=mejor..31=peor).
set -euo pipefail

if [[ $# -lt 1 || ! -d "$1" ]]; then
  sed -n '2,13p' "$0" >&2
  exit 1
fi

carpeta="$(cd "$1" && pwd)"
nombre="${2:-$(basename "$carpeta")}"
if [[ ! "$nombre" =~ ^[a-z0-9_-]+$ ]]; then
  echo "Nombre inválido '$nombre': usa solo minúsculas, números, - y _" >&2
  exit 1
fi
seg_min="${SEGUNDOS:-2}"
seg_max="${MAX_SEGUNDOS:-4}"
fps="${FPS:-10}"
calidad="${CALIDAD:-10}"
raiz="$(cd "$(dirname "$0")/.." && pwd)"
salida="$raiz/data"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
LADO=320
CAJA=224

# Efectos por estado, aplicados al personaje (con transparencia) y a su posición.
declare -A EFECTO=(
  [reposo]="" [pensando]="" [feliz]="" [listo]="" [despierta]="fade=in:st=0:d=1.5:alpha=1"
  [durmiendo]="setpts=1.8*PTS,colorchannelmixer=rr=0.65:gg=0.65:bb=0.8"
  [error]="colorchannelmixer=rr=1:rg=0.35:rb=0.35:gr=0.15:gg=0.25:gb=0.15:br=0.15:bg=0.15:bb=0.25"
)
declare -A POS_Y=(
  [feliz]="(H-h)/2-abs(18*sin(t*6))" [listo]="(H-h)/2-abs(40*sin(t*7))"
)
declare -A POS_X=([error]="(W-w)/2+6*sin(t*45)")
declare -A TEXTO=(
  [pensando]="drawtext=text='?':x=244:y=58+8*sin(t*4):fontsize=64:fontcolor=white:borderw=4:bordercolor=black"
  [durmiendo]="drawtext=text='z':x=236+8*sin(t*2):y=90-mod(t*30\,60):fontsize=44:fontcolor=0x87CEEB:borderw=3:bordercolor=black:alpha='1-mod(t*30\,60)/60'"
  [listo]="drawtext=text='!':x=246:y=50:fontsize=70:fontcolor=yellow:borderw=4:bordercolor=black"
)

buscar() { find "$carpeta" -maxdepth 1 -type f -name "$1.*" | head -1; }

# Duración del clip: ciclos completos de la fuente hasta llegar a seg_min, sin pasar de seg_max.
duracion() {
  local d
  d="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$1" 2>/dev/null || true)"
  awk -v d="${d:-0}" -v min="$seg_min" -v max="$seg_max" 'BEGIN {
    if (d <= 0 || d == "N/A") { print min; exit }
    n = int(min / d); if (n * d < min) n++
    t = n * d; if (t > max) t = max; printf "%.2f", t }'
}

# convertir <archivo fuente> <estado destino>
convertir() {
  local fuente="$1" estado="$2" base modo entrada escala filtro bucle
  base="$(basename "$fuente")"
  modo="$(echo "$base" | awk -F. 'NF>2{print $2}')"
  entrada="$fuente"

  if [[ "$modo" == blanco ]]; then
    magick "$fuente" -coalesce -bordercolor white -border 1 -fuzz 12% \
      -fill none -draw "color 0,0 floodfill" -shave 1x1 "$tmp/$estado.gif"
    entrada="$tmp/$estado.gif"
  fi

  bucle=(-stream_loop -1)
  [[ "$entrada" == *.gif ]] && bucle=(-ignore_loop 0)

  local seg
  seg="$(duracion "$entrada")"
  local efecto="${EFECTO[$estado]:-}" px="${POS_X[$estado]:-(W-w)/2}" py="${POS_Y[$estado]:-(H-h)/2}"
  if [[ "$modo" == foto ]]; then
    escala="scale=$LADO:$LADO:force_original_aspect_ratio=increase,crop=$LADO:$LADO"
  elif [[ "$modo" == pixel ]]; then
    escala="scale=$CAJA:$CAJA:force_original_aspect_ratio=decrease:flags=neighbor"
  else
    escala="scale=$CAJA:$CAJA:force_original_aspect_ratio=decrease"
  fi

  filtro="[0:v]${efecto:+$efecto,}fps=$fps,format=rgba,$escala[p];"
  filtro+="color=c=magenta:s=${LADO}x${LADO}:r=$fps[bg];[bg][p]overlay=x='$px':y='$py':shortest=1"
  [[ -n "${TEXTO[$estado]:-}" ]] && filtro+=",${TEXTO[$estado]}"

  ffmpeg -hide_banner -loglevel error -y "${bucle[@]}" -t "$seg" -i "$entrada" \
    -filter_complex "$filtro,format=yuvj420p" -t "$seg" -q:v "$calidad" -an -f mjpeg "$salida/$nombre@$estado.mjpeg"
  printf "  %-10s %-28s %4s KB\n" "$estado" "$base" "$(du -k "$salida/$nombre@$estado.mjpeg" | cut -f1)"
}

reposo="$(buscar reposo)"
[[ -z "$reposo" ]] && { echo "Falta reposo.<ext> en $carpeta" >&2; exit 1; }
mkdir -p "$salida" && rm -f "$salida/$nombre@"*.mjpeg
echo "Avatar '$nombre':"
for estado in reposo pensando feliz durmiendo error listo despierta; do
  fuente="$(buscar "$estado")"
  if [[ -n "$fuente" ]]; then
    convertir "$fuente" "$estado"
  else
    convertir "$reposo" "$estado"
  fi
done
echo "Total: $(du -ck "$salida/$nombre@"*.mjpeg | tail -1 | cut -f1) KB. Ocupado en data/: $(du -sk "$raiz/data" | cut -f1) KB de ~10100 KB."
echo "Sube con: systemctl --user stop orion-cara && pio run -t uploadfs && systemctl --user start orion-cara"
