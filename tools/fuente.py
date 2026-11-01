#!/usr/bin/env python3
"""Convierte fuentes TTF al formato de fuente suave (.vlw) de TFT_eSPI y genera src/fuentes.h.

Uso: tools/fuente.py            (usa las fuentes y tamaños de FUENTES)
Incluye ASCII imprimible y los caracteres del español (tildes, ñ, ü, ¡, ¿) más unos símbolos.
"""
import struct
import sys
from pathlib import Path

from PIL import ImageFont

RAIZ = Path(__file__).resolve().parent.parent
SALIDA = RAIZ / "src" / "fuentes.h"

# (nombre del arreglo en C, archivo TTF, alto en píxeles)
FUENTES = [
    ("FUENTE_CAJA", "/usr/share/fonts/noto/NotoSans-Medium.ttf", 21),
    ("FUENTE_ESTADO", "/usr/share/fonts/noto/NotoSans-Regular.ttf", 15),
]
CARACTERES = [chr(c) for c in range(0x21, 0x7F)] + list("¡¿°·áéíóúÁÉÍÓÚñÑüÜ…")


def vlw(ttf, tam):
    """Bytes .vlw: cabecera, métricas de cada glifo (7 enteros big-endian) y luego los bitmaps."""
    fuente = ImageFont.truetype(ttf, tam)
    ascenso, descenso = fuente.getmetrics()
    metricas, bitmaps = [], []
    for ch in sorted(CARACTERES, key=ord):
        mascara, (dx, dy) = fuente.getmask2(ch, mode="L", anchor="ls")
        ancho, alto = mascara.size
        avance = round(fuente.getlength(ch))
        metricas.append(struct.pack(">7i", ord(ch), alto, ancho, avance, -dy, dx, 0))
        bitmaps.append(bytes(mascara))
    cabecera = struct.pack(">6i", len(metricas), 11, tam, 0, ascenso, descenso)
    return cabecera + b"".join(metricas) + b"".join(bitmaps)


def arreglo_c(nombre, datos):
    filas = [", ".join(f"0x{b:02x}" for b in datos[i:i + 24]) for i in range(0, len(datos), 24)]
    return f"const uint8_t {nombre}[] = {{\n  " + ",\n  ".join(filas) + "\n};\n"


def main():
    partes = ["// Generado por tools/fuente.py a partir de Noto Sans (SIL Open Font License 1.1).\n",
              "#pragma once\n#include <stdint.h>\n\n"]
    for nombre, ttf, tam in FUENTES:
        if not Path(ttf).exists():
            sys.exit(f"No encuentro {ttf}")
        datos = vlw(ttf, tam)
        partes.append(arreglo_c(nombre, datos))
        print(f"{nombre}: {Path(ttf).name} {tam}px, {len(datos)} bytes")
    SALIDA.write_text("".join(partes))
    print(f"Escrito {SALIDA.relative_to(RAIZ)}")


if __name__ == "__main__":
    main()
