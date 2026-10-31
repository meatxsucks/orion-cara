#pragma once
#include <TFT_eSPI.h>

enum Expresion { CHORO, FELIZ, PENSANDO, DURMIENDO, ERROR_, NUM_EXPRESIONES };

const char *nombreExpresion(Expresion e);
bool expresionPorNombre(const char *nombre, Expresion &salida);

// Cara animada del mono dibujada en un sprite (PSRAM) para evitar parpadeo.
class Cara {
 public:
  explicit Cara(TFT_eSPI &tft);
  bool begin(int x, int y);
  void setExpresion(Expresion e);
  Expresion expresion() const { return expr_; }
  void actualizar(uint32_t ahora);

 private:
  void dibujar(uint32_t ahora);
  void dibujarOjos(int cx, int cy, float apertura);
  void dibujarLentes(int cx, int cy);
  void dibujarBoca(int cx, int cy, uint32_t ahora);
  void curva(int cx, int cy, int ancho, int alto, int grosor, uint16_t color);

  TFT_eSPI &tft_;
  TFT_eSprite spr_;
  int x_ = 0, y_ = 0;
  Expresion expr_ = CHORO;
  uint32_t proximoPestaneo_ = 0, inicioPestaneo_ = 0;
  uint32_t proximaMirada_ = 0;
  float miradaX_ = 0, miradaY_ = 0, objetivoX_ = 0, objetivoY_ = 0;
};
