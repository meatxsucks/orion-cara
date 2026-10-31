#include "clips.h"
#include <FS.h>
#include <FFat.h>
using fs::File;
#include <JPEGDEC.h>
#include <vector>

namespace clips {
namespace {

TFT_eSPI *pantalla = nullptr;
JPEGDEC jpeg;
std::vector<String> lista;

int dibujarBloque(JPEGDRAW *b) {
  pantalla->pushImage(b->x, b->y, b->iWidth, b->iHeight, b->pPixels);
  return 1;
}

// Carga un archivo completo en PSRAM.
uint8_t *cargar(const String &nombre, size_t &tam) {
  File f = FFat.open("/" + nombre);
  if (!f) return nullptr;
  tam = f.size();
  uint8_t *datos = (uint8_t *)ps_malloc(tam);
  if (datos) f.read(datos, tam);
  f.close();
  return datos;
}

// Busca el siguiente JPEG desde pos; deja [ini, fin) y devuelve false si no hay más.
bool siguienteJpeg(const uint8_t *d, size_t tam, size_t pos, size_t &ini, size_t &fin) {
  while (pos + 1 < tam && !(d[pos] == 0xFF && d[pos + 1] == 0xD8)) pos++;
  if (pos + 1 >= tam) return false;
  ini = pos;
  fin = pos + 2;
  while (fin + 1 < tam && !(d[fin] == 0xFF && d[fin + 1] == 0xD9)) fin++;
  fin = min(fin + 2, tam);
  return true;
}

void decodificar(const uint8_t *d, size_t len, int x, int y) {
  if (!jpeg.openRAM((uint8_t *)d, len, dibujarBloque)) return;
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  pantalla->setSwapBytes(true);
  pantalla->startWrite();
  jpeg.decode(x, y, 0);
  pantalla->endWrite();
  pantalla->setSwapBytes(false);
  jpeg.close();
}

}  // namespace

bool begin(TFT_eSPI &tft) {
  pantalla = &tft;
  if (!FFat.begin(false)) return false;
  File raiz = FFat.open("/");
  for (File f = raiz.openNextFile(); f; f = raiz.openNextFile()) {
    String n = f.name();
    if (n.endsWith(".mjpeg")) lista.push_back(n.startsWith("/") ? n.substring(1) : n);
  }
  return true;
}

int cantidad() { return lista.size(); }

bool existe(const String &nombre) { return FFat.exists("/" + nombre); }

String nombre(int indice) { return lista.at(indice); }

String aleatorio() {
  std::vector<String> sueltos;
  for (auto &n : lista)
    if (!n.startsWith("cara_")) sueltos.push_back(n);
  return sueltos.empty() ? String() : sueltos[random(sueltos.size())];
}

float reproducir(const String &nombre, int x, int y, uint16_t fpsObjetivo) {
  size_t tam;
  uint8_t *datos = cargar(nombre, tam);
  if (!datos) return -1;

  const uint32_t msPorCuadro = 1000 / fpsObjetivo;
  uint32_t inicio = millis(), cuadros = 0;
  size_t a, b, pos = 0;
  while (siguienteJpeg(datos, tam, pos, a, b)) {
    uint32_t t0 = millis();
    decodificar(datos + a, b - a, x, y);
    cuadros++;
    uint32_t gastado = millis() - t0;
    if (gastado < msPorCuadro) delay(msPorCuadro - gastado);
    pos = b;
  }
  free(datos);

  uint32_t total = millis() - inicio;
  return total ? cuadros * 1000.0f / total : 0;
}

bool Bucle::abrir(const String &nombre) {
  cerrar();
  datos_ = cargar(nombre, tam_);
  pos_ = 0;
  nombre_ = datos_ ? nombre : String();
  return datos_ != nullptr;
}

void Bucle::cerrar() {
  free(datos_);
  datos_ = nullptr;
  nombre_ = "";
}

void Bucle::cuadro(int x, int y) {
  if (!datos_) return;
  size_t a, b;
  if (!siguienteJpeg(datos_, tam_, pos_, a, b)) {
    pos_ = 0;
    if (!siguienteJpeg(datos_, tam_, pos_, a, b)) return;
  }
  decodificar(datos_ + a, b - a, x, y);
  pos_ = b;
}

}  // namespace clips
