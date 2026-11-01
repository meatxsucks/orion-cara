#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Recuadro de diálogo y línea de estado. Cada uno se dibuja completo en un sprite (PSRAM)
// y se empuja de una vez: no hay parpadeo ni restos de textos anteriores.
namespace panel {

struct Rect {
  int x, y, w, h;
};

struct Colores {
  uint16_t caja, borde, letras, estado, pantalla;
};

void begin(TFT_eSPI &tft);

// Posición y tamaño de cada zona; recrea los sprites y redibuja.
void ubicar(const Rect &caja, const Rect &estado);

void setColores(const Colores &c);

// Texto del recuadro (UTF-8); se reparte en líneas y se recorta con "…" si no cabe.
void caja(const String &texto);

// Datos separados por dos espacios ("LLM ok  CPU 10%"); se agrupan en líneas centradas.
void estado(const String &texto);

}  // namespace panel
