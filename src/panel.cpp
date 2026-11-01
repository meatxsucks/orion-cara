#include "panel.h"

#include <vector>

#include "fuentes.h"

namespace panel {
namespace {

const int RADIO = 12, SOMBRA = 4, MARGEN = 14;
const char *SEPARADOR = "  ·  ";

TFT_eSPI *pantalla = nullptr;
TFT_eSprite *sprCaja = nullptr, *sprEstado = nullptr;
Rect rCaja{}, rEstado{};
Colores col{};
String textoCaja, textoEstado;

void crear(TFT_eSprite *&spr, const Rect &r, const uint8_t *fuente) {
  if (!spr) {
    spr = new TFT_eSprite(pantalla);
    spr->setColorDepth(16);
    spr->loadFont(fuente);
  }
  spr->deleteSprite();
  spr->createSprite(r.w, r.h);
}

// Quita el último carácter UTF-8 completo.
void quitarUltimo(String &s) {
  int i = s.length() - 1;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
  s.remove(i < 0 ? 0 : i);
}

// Reparte palabras en líneas que caben en `ancho`.
std::vector<String> envolver(TFT_eSprite &s, const String &texto, int ancho) {
  std::vector<String> lineas;
  String actual;
  int desde = 0;
  while (desde <= (int)texto.length()) {
    int fin = texto.indexOf(' ', desde);
    if (fin < 0) fin = texto.length();
    String palabra = texto.substring(desde, fin);
    desde = fin + 1;
    if (palabra.isEmpty()) continue;
    String prueba = actual.isEmpty() ? palabra : actual + " " + palabra;
    if (s.textWidth(prueba) <= ancho || actual.isEmpty()) {
      actual = prueba;
    } else {
      lineas.push_back(actual);
      actual = palabra;
    }
  }
  if (actual.length()) lineas.push_back(actual);
  return lineas;
}

void recortarConPuntos(TFT_eSprite &s, String &linea, int ancho) {
  while (linea.length() && s.textWidth(linea + "…") > ancho) quitarUltimo(linea);
  linea += "…";
}

void dibujarCaja() {
  if (!sprCaja) return;
  TFT_eSprite &s = *sprCaja;
  int w = s.width() - SOMBRA, h = s.height() - SOMBRA;
  s.fillSprite(col.pantalla);
  s.fillRoundRect(SOMBRA, SOMBRA, w, h, RADIO, s.alphaBlend(120, TFT_BLACK, col.pantalla));
  s.fillRoundRect(0, 0, w, h, RADIO, col.caja);
  s.drawRoundRect(0, 0, w, h, RADIO, col.borde);
  s.drawRoundRect(1, 1, w - 2, h - 2, RADIO - 1, col.borde);
  s.drawRoundRect(5, 5, w - 10, h - 10, RADIO - 4, s.alphaBlend(90, col.borde, col.caja));

  int ancho = w - 2 * MARGEN, alto = s.fontHeight();
  auto lineas = envolver(s, textoCaja, ancho);
  size_t maximo = max(1, (h - 12) / alto);
  if (lineas.size() > maximo) {
    lineas.resize(maximo);
    recortarConPuntos(s, lineas.back(), ancho);
  }
  s.setTextColor(col.letras, col.caja);
  s.setTextDatum(TC_DATUM);
  int y = (h - (int)lineas.size() * alto) / 2 + 1;
  for (auto &l : lineas) {
    s.drawString(l, w / 2, y);
    y += alto;
  }
  s.pushSprite(rCaja.x, rCaja.y);
}

void dibujarEstado() {
  if (!sprEstado) return;
  TFT_eSprite &s = *sprEstado;
  s.fillSprite(col.pantalla);
  int ancho = s.width() - 8, alto = s.fontHeight();
  std::vector<String> lineas;
  String actual;
  int desde = 0;
  while (desde < (int)textoEstado.length()) {
    int fin = textoEstado.indexOf("  ", desde);
    if (fin < 0) fin = textoEstado.length();
    String dato = textoEstado.substring(desde, fin);
    dato.trim();
    desde = fin + 2;
    if (dato.isEmpty()) continue;
    String prueba = actual.isEmpty() ? dato : actual + SEPARADOR + dato;
    if (s.textWidth(prueba) <= ancho || actual.isEmpty()) {
      actual = prueba;
    } else {
      lineas.push_back(actual);
      actual = dato;
    }
  }
  if (actual.length()) lineas.push_back(actual);
  size_t maximo = max(1, s.height() / alto);
  if (lineas.size() > maximo) lineas.resize(maximo);

  s.setTextColor(col.estado, col.pantalla);
  s.setTextDatum(TC_DATUM);
  int y = (s.height() - (int)lineas.size() * alto) / 2;
  for (auto &l : lineas) {
    s.drawString(l, s.width() / 2, y);
    y += alto;
  }
  s.pushSprite(rEstado.x, rEstado.y);
}

}  // namespace

void begin(TFT_eSPI &tft) { pantalla = &tft; }

void ubicar(const Rect &caja, const Rect &estado) {
  rCaja = caja;
  rEstado = estado;
  crear(sprCaja, rCaja, FUENTE_CAJA);
  crear(sprEstado, rEstado, FUENTE_ESTADO);
  dibujarCaja();
  dibujarEstado();
}

void setColores(const Colores &c) {
  col = c;
  dibujarCaja();
  dibujarEstado();
}

void caja(const String &texto) {
  if (texto == textoCaja) return;
  textoCaja = texto;
  dibujarCaja();
}

void estado(const String &texto) {
  if (texto == textoEstado) return;
  textoEstado = texto;
  dibujarEstado();
}

}  // namespace panel
