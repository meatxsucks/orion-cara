#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>

// Reproduce clips MJPEG (JPEGs concatenados) guardados en la partición FFat.
namespace clips {

// Zona de pantalla donde se centra un clip; si no cabe, se dibuja a la mitad de tamaño.
struct Area {
  int x, y, w, h;
};

// Cómo se pinta cada cuadro: el color clave (magenta) se reemplaza por `fondo`, y con
// `circulo` todo lo que queda fuera del círculo toma el color de `pantalla`, con un anillo.
struct Estilo {
  bool circulo;
  uint16_t fondo, anillo, pantalla;
};

bool begin(TFT_eSPI &tft);
void setEstilo(const Estilo &e);
bool existe(const String &ruta);
std::vector<String> avatares();
// Bloquea mientras dura el clip; devuelve los fps medidos o -1 si falló.
float reproducir(const String &ruta, const Area &area, uint16_t fpsObjetivo = 10);

// Clip en loop que avanza un cuadro por llamada, para usarlo como cara animada.
class Bucle {
 public:
  bool abrir(const String &ruta);
  void cerrar();
  bool activo() const { return datos_ != nullptr; }
  const String &ruta() const { return ruta_; }
  void cuadro(const Area &area);

 private:
  uint8_t *datos_ = nullptr;
  size_t tam_ = 0, pos_ = 0;
  String ruta_;
};

}  // namespace clips
