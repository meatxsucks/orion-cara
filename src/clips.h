#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Reproduce clips MJPEG (JPEGs concatenados) guardados en la partición FFat.
namespace clips {

bool begin(TFT_eSPI &tft);
int cantidad();
bool existe(const String &nombre);
String nombre(int indice);
String aleatorio();
// Bloquea mientras dura el clip; devuelve los fps medidos o -1 si falló.
float reproducir(const String &nombre, int x, int y, uint16_t fpsObjetivo = 12);

// Clip en loop que avanza un cuadro por llamada, para usarlo como cara animada.
class Bucle {
 public:
  bool abrir(const String &nombre);
  void cerrar();
  bool activo() const { return datos_ != nullptr; }
  const String &nombre() const { return nombre_; }
  void cuadro(int x, int y);

 private:
  uint8_t *datos_ = nullptr;
  size_t tam_ = 0, pos_ = 0;
  String nombre_;
};

}  // namespace clips
