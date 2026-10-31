#include "cara.h"
#include <math.h>

namespace {
const int ANCHO = 300;
const int ALTO = 250;
const uint16_t FONDO = 0x18E3;
const uint16_t PIEL = 0x8A22;
const uint16_t CARA = 0xEE11;
const uint16_t ROSADO = 0xFB2C;
const char *NOMBRES[] = {"choro", "feliz", "pensando", "durmiendo", "error"};
}  // namespace

const char *nombreExpresion(Expresion e) { return NOMBRES[e]; }

bool expresionPorNombre(const char *nombre, Expresion &salida) {
  for (int i = 0; i < NUM_EXPRESIONES; i++) {
    if (strcasecmp(nombre, NOMBRES[i]) == 0) {
      salida = (Expresion)i;
      return true;
    }
  }
  return false;
}

Cara::Cara(TFT_eSPI &tft) : tft_(tft), spr_(&tft) {}

bool Cara::begin(int x, int y) {
  x_ = x;
  y_ = y;
  spr_.setColorDepth(16);
  return spr_.createSprite(ANCHO, ALTO) != nullptr;
}

void Cara::setExpresion(Expresion e) { expr_ = e; }

void Cara::actualizar(uint32_t ahora) {
  if (ahora >= proximoPestaneo_) {
    inicioPestaneo_ = ahora;
    proximoPestaneo_ = ahora + random(2000, 6000);
  }
  if (ahora >= proximaMirada_) {
    objetivoX_ = random(-7, 8);
    objetivoY_ = random(-4, 5);
    proximaMirada_ = ahora + random(800, 3500);
  }
  miradaX_ += (objetivoX_ - miradaX_) * 0.25f;
  miradaY_ += (objetivoY_ - miradaY_) * 0.25f;
  dibujar(ahora);
  spr_.pushSprite(x_, y_);
}

void Cara::curva(int cx, int cy, int ancho, int alto, int grosor, uint16_t color) {
  float px = cx - ancho, py = cy;
  for (int i = 1; i <= 16; i++) {
    float t = -1.0f + 2.0f * i / 16;
    float x = cx + t * ancho;
    float y = cy + alto * (1 - t * t);
    spr_.drawWideLine(px, py, x, y, grosor, color, CARA);
    px = x;
    py = y;
  }
}

void Cara::dibujarOjos(int cx, int cy, float apertura) {
  for (int lado = -1; lado <= 1; lado += 2) {
    int ox = cx + lado * 32, oy = cy - 22;
    if (expr_ == DURMIENDO || apertura < 0.15f) {
      spr_.drawWideLine(ox - 14, oy, ox + 14, oy, 4, TFT_BLACK, CARA);
      continue;
    }
    if (expr_ == ERROR_) {
      spr_.drawWideLine(ox - 12, oy - 12, ox + 12, oy + 12, 5, TFT_BLACK, CARA);
      spr_.drawWideLine(ox - 12, oy + 12, ox + 12, oy - 12, 5, TFT_BLACK, CARA);
      continue;
    }
    int alto = 17 * apertura;
    spr_.fillEllipse(ox, oy, 15, alto, TFT_WHITE);
    float mx = miradaX_, my = miradaY_;
    if (expr_ == PENSANDO) {
      mx = -6;
      my = -7;
    }
    int py = constrain(oy + (int)my, oy - alto + 6, oy + alto - 6);
    spr_.fillCircle(ox + mx, py, 8, TFT_BLACK);
    spr_.fillCircle(ox + mx + 3, py - 3, 2, TFT_WHITE);
  }
  if (expr_ == PENSANDO) {
    spr_.drawWideLine(cx + 16, cy - 52, cx + 48, cy - 60, 4, PIEL, CARA);
    spr_.drawWideLine(cx - 48, cy - 50, cx - 16, cy - 50, 4, PIEL, CARA);
  }
}

void Cara::dibujarLentes(int cx, int cy) {
  spr_.fillRoundRect(cx - 70, cy - 40, 62, 34, 10, TFT_BLACK);
  spr_.fillRoundRect(cx + 8, cy - 40, 62, 34, 10, TFT_BLACK);
  spr_.fillRect(cx - 10, cy - 34, 20, 6, TFT_BLACK);
  spr_.drawWideLine(cx - 60, cy - 32, cx - 44, cy - 32, 3, TFT_WHITE, TFT_BLACK);
  spr_.drawWideLine(cx + 18, cy - 32, cx + 34, cy - 32, 3, TFT_WHITE, TFT_BLACK);
}

void Cara::dibujarBoca(int cx, int cy, uint32_t ahora) {
  int by = cy + 32;
  switch (expr_) {
    case CHORO:
      curva(cx + 12, by, 26, 9, 5, PIEL);
      break;
    case FELIZ:
      spr_.fillEllipse(cx, by + 6, 34, 22, TFT_BLACK);
      spr_.fillRect(cx - 36, by - 18, 72, 24, CARA);
      spr_.fillEllipse(cx, by + 20, 16, 7, ROSADO);
      break;
    case PENSANDO:
      spr_.drawWideLine(cx - 4, by + 10, cx + 22, by + 4, 5, PIEL, CARA);
      for (int i = 0; i < 3; i++) {
        bool activo = (ahora / 400) % 4 > (uint32_t)i;
        spr_.fillCircle(cx + 105 + i * 18, cy - 95 + i * -6, 5 + i * 2, activo ? TFT_WHITE : FONDO);
      }
      break;
    case DURMIENDO: {
      spr_.fillEllipse(cx, by + 8, 9, 7 + 2 * sinf(ahora / 500.0f), PIEL);
      int fase = (ahora / 600) % 3;
      spr_.setTextColor(TFT_SKYBLUE, FONDO);
      spr_.setTextDatum(MC_DATUM);
      for (int i = 0; i <= fase; i++) {
        spr_.drawString("z", cx + 100 + i * 18, cy - 70 - i * 20, 4);
      }
      break;
    }
    case ERROR_: {
      float px = cx - 30, py = by + 8;
      for (int i = 1; i <= 12; i++) {
        float x = cx - 30 + i * 5;
        float y = by + 8 + 5 * sinf(i * 1.3f);
        spr_.drawWideLine(px, py, x, y, 4, PIEL, CARA);
        px = x;
        py = y;
      }
      break;
    }
    default:
      break;
  }
}

void Cara::dibujar(uint32_t ahora) {
  spr_.fillSprite(FONDO);
  int cx = ANCHO / 2;
  int cy = ALTO / 2 + 8;

  spr_.fillCircle(cx - 95, cy - 10, 38, PIEL);
  spr_.fillCircle(cx + 95, cy - 10, 38, PIEL);
  spr_.fillCircle(cx - 95, cy - 10, 22, CARA);
  spr_.fillCircle(cx + 95, cy - 10, 22, CARA);
  spr_.fillCircle(cx, cy, 95, PIEL);
  spr_.fillEllipse(cx, cy + 30, 72, 58, CARA);
  spr_.fillCircle(cx - 32, cy - 22, 30, CARA);
  spr_.fillCircle(cx + 32, cy - 22, 30, CARA);

  if (expr_ == CHORO) {
    dibujarLentes(cx, cy);
  } else {
    uint32_t t = ahora - inicioPestaneo_;
    float apertura = 1.0f;
    if (t < 160) apertura = fabsf(t - 80.0f) / 80.0f;
    dibujarOjos(cx, cy, apertura);
  }

  spr_.fillCircle(cx - 10, cy + 10, 4, PIEL);
  spr_.fillCircle(cx + 10, cy + 10, 4, PIEL);
  if (expr_ == FELIZ) {
    spr_.fillEllipse(cx - 58, cy + 20, 12, 7, ROSADO);
    spr_.fillEllipse(cx + 58, cy + 20, 12, 7, ROSADO);
  }
  dibujarBoca(cx, cy, ahora);
}
