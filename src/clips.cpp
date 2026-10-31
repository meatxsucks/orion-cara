#include "clips.h"
#include <FS.h>
#include <FFat.h>
using fs::File;
#include <JPEGDEC.h>

namespace clips {
namespace {

const int ANCHO_ANILLO = 6;

TFT_eSPI *pantalla = nullptr;
JPEGDEC jpeg;
Estilo estilo = {true, 0x1A9F, 0x5DDF, 0x18E3};
int centroX = 0, centroY = 0, radio = 0;

// Magenta aproximado: tolera los artefactos de compresión JPEG en los bordes.
inline bool esClave(uint16_t p) {
  uint8_t r = p >> 11, g = (p >> 5) & 0x3F, b = p & 0x1F;
  return r >= 20 && b >= 20 && g <= 28;
}

int dibujarBloque(JPEGDRAW *b) {
  uint16_t *px = b->pPixels;
  const int r2 = radio * radio, ri2 = (radio - ANCHO_ANILLO) * (radio - ANCHO_ANILLO);
  for (int j = 0; j < b->iHeight; j++) {
    int dy = b->y + j - centroY;
    for (int i = 0; i < b->iWidth; i++, px++) {
      if (estilo.circulo) {
        int dx = b->x + i - centroX, d2 = dx * dx + dy * dy;
        if (d2 > r2) {
          *px = estilo.pantalla;
          continue;
        }
        if (d2 > ri2) {
          *px = estilo.anillo;
          continue;
        }
        if (esClave(*px)) *px = estilo.fondo;
      } else if (esClave(*px)) {
        *px = estilo.pantalla;
      }
    }
  }
  pantalla->pushImage(b->x, b->y, b->iWidth, b->iHeight, b->pPixels);
  return 1;
}

// Carga un archivo completo en PSRAM.
uint8_t *cargar(const String &ruta, size_t &tam) {
  File f = FFat.open("/" + ruta);
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

void decodificar(const uint8_t *d, size_t len, const Area &a) {
  if (!jpeg.openRAM((uint8_t *)d, len, dibujarBloque)) return;
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  int w = jpeg.getWidth(), h = jpeg.getHeight(), escala = 0;
  if (w > a.w || h > a.h) {
    escala = JPEG_SCALE_HALF;
    w /= 2;
    h /= 2;
  }
  int x = a.x + (a.w - w) / 2, y = a.y + (a.h - h) / 2;
  centroX = x + w / 2;
  centroY = y + h / 2;
  radio = min(w, h) / 2 - 1;
  pantalla->setSwapBytes(true);
  pantalla->startWrite();
  jpeg.decode(x, y, escala);
  pantalla->endWrite();
  pantalla->setSwapBytes(false);
  jpeg.close();
}

}  // namespace

bool begin(TFT_eSPI &tft) {
  pantalla = &tft;
  return FFat.begin(false);
}

void setEstilo(const Estilo &e) { estilo = e; }

bool existe(const String &ruta) { return FFat.exists("/" + ruta); }

std::vector<String> avatares() {
  std::vector<String> lista;
  File raiz = FFat.open("/");
  for (File f = raiz.openNextFile(); f; f = raiz.openNextFile()) {
    String n = f.name();
    n = n.substring(n.lastIndexOf('/') + 1);
    if (n.endsWith("@reposo.mjpeg")) lista.push_back(n.substring(0, n.indexOf('@')));
  }
  std::sort(lista.begin(), lista.end(), [](const String &a, const String &b) { return a < b; });
  return lista;
}

float reproducir(const String &ruta, const Area &area, uint16_t fpsObjetivo) {
  size_t tam;
  uint8_t *datos = cargar(ruta, tam);
  if (!datos) return -1;

  const uint32_t msPorCuadro = 1000 / fpsObjetivo;
  uint32_t inicio = millis(), cuadros = 0;
  size_t a, b, pos = 0;
  while (siguienteJpeg(datos, tam, pos, a, b)) {
    uint32_t t0 = millis();
    decodificar(datos + a, b - a, area);
    cuadros++;
    uint32_t gastado = millis() - t0;
    if (gastado < msPorCuadro) delay(msPorCuadro - gastado);
    pos = b;
  }
  free(datos);

  uint32_t total = millis() - inicio;
  return total ? cuadros * 1000.0f / total : 0;
}

bool Bucle::abrir(const String &ruta) {
  cerrar();
  datos_ = cargar(ruta, tam_);
  pos_ = 0;
  ruta_ = datos_ ? ruta : String();
  return datos_ != nullptr;
}

void Bucle::cerrar() {
  free(datos_);
  datos_ = nullptr;
  ruta_ = "";
}

void Bucle::cuadro(const Area &area) {
  if (!datos_) return;
  size_t a, b;
  if (!siguienteJpeg(datos_, tam_, pos_, a, b)) {
    pos_ = 0;
    if (!siguienteJpeg(datos_, tam_, pos_, a, b)) return;
  }
  decodificar(datos_ + a, b - a, area);
  pos_ = b;
}

}  // namespace clips
