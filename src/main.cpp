#include <Arduino.h>
#include <FFat.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <Wire.h>

#include <vector>

#include "cara.h"
#include "clips.h"
#include "panel.h"
#include "red.h"

TFT_eSPI tft;
Cara cara(tft);
clips::Bucle skin;
Preferences prefs;

const uint16_t FONDO = 0x18E3;
const uint16_t COLOR_ESTADO = 0xAD55;
const uint8_t TACTIL_DIR = 0x38;
const uint32_t CADA_CUADRO_MS = 50;
const uint32_t CADA_CUADRO_SKIN_MS = 100;
const uint32_t SIN_PC_MS = 30000;
const uint32_t MANTENER_MS = 700;
const uint32_t AVISO_MS = 2500;

// Posiciones de cada elemento según la orientación de la pantalla.
struct Layout {
  int caraX, caraY;
  clips::Area zona;
  panel::Rect caja, estado;
};
const Layout HORIZONTAL = {10, 35, {0, 0, 320, 320}, {324, 10, 152, 178}, {320, 194, 160, 120}};
const Layout VERTICAL = {10, 35, {0, 0, 320, 320}, {12, 326, 300, 88}, {0, 416, 320, 64}};
Layout L = HORIZONTAL;
bool vertical = false;

// Frases por defecto para cada expresión; un avatar puede traer las suyas en <avatar>@frases.txt.
const char *FRASES_NEUTRAS[NUM_EXPRESIONES] = {"Aquí estoy", "¡Listo!", "Pensando…", "Descansando… zzz",
                                               "Algo salió mal"};
std::vector<String> frases[NUM_EXPRESIONES];

// Colores con nombre para el círculo, el anillo y el recuadro.
struct ColorNombrado {
  const char *nombre;
  uint8_t r, g, b;
};
const ColorNombrado PALETA[] = {
    {"azul", 21, 101, 192},     {"celeste", 41, 182, 246}, {"morado", 106, 27, 154},
    {"rosado", 216, 27, 96},    {"rojo", 198, 40, 40},     {"naranja", 239, 108, 0},
    {"amarillo", 249, 168, 37}, {"verde", 46, 125, 50},    {"gris", 84, 110, 122},
    {"negro", 0, 0, 0},         {"blanco", 235, 235, 235}, {"pergamino", 243, 228, 196},
    {"cafe", 121, 85, 72},      {"chocolate", 62, 39, 35},
};
const int NUM_COLORES = sizeof(PALETA) / sizeof(PALETA[0]);

String avatar;
int indiceFondo = 0;
uint16_t colorAnillo = 0, colorCaja = 0, colorBorde = 0, colorLetras = 0;
bool circulo = true;

bool modoDemo = true;
String textoLibre, fraseActual;
uint32_t avisoHasta = 0;
uint32_t proximoCambio = 0, proximoCuadro = 0, proximoEstadoDemo = 0;
bool tocandoAntes = false, mantenidoAplicado = false;
uint32_t inicioToque = 0;
uint32_t ultimoComando = 0;

// Lee el FT6236: true si hay al menos un dedo en la pantalla.
bool hayToque() {
  Wire.beginTransmission(TACTIL_DIR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TACTIL_DIR, (uint8_t)1) != 1) return false;
  uint8_t n = Wire.read() & 0x0F;
  return n > 0 && n <= 2;
}

uint16_t colorDe(const ColorNombrado &c) { return tft.color565(c.r, c.g, c.b); }

uint16_t colorPorNombre(const char *nombre) {
  for (auto &c : PALETA)
    if (strcmp(c.nombre, nombre) == 0) return colorDe(c);
  return TFT_WHITE;
}

// Acepta un nombre de la paleta o "#RRGGBB"; devuelve false si no lo reconoce.
bool parsearColor(const String &txt, uint16_t &salida, int *indice = nullptr) {
  if (txt.startsWith("#") && txt.length() == 7) {
    long v = strtol(txt.c_str() + 1, nullptr, 16);
    salida = tft.color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    if (indice) *indice = -1;
    return true;
  }
  for (int i = 0; i < NUM_COLORES; i++) {
    if (txt.equalsIgnoreCase(PALETA[i].nombre)) {
      salida = colorDe(PALETA[i]);
      if (indice) *indice = i;
      return true;
    }
  }
  return false;
}

uint16_t colorFondo() {
  return indiceFondo >= 0 ? colorDe(PALETA[indiceFondo]) : prefs.getUShort("fondo_hex", 0);
}

void aplicarEstilo() { clips::setEstilo({circulo, colorFondo(), colorAnillo, FONDO}); }

void aplicarColoresPanel() { panel::setColores({colorCaja, colorBorde, colorLetras, COLOR_ESTADO, FONDO}); }

String archivoAvatar(const String &nombre, const String &estadoArchivo) {
  return nombre + "@" + estadoArchivo + ".mjpeg";
}

const char *archivoExpresion(Expresion e) { return e == CHORO ? "reposo" : nombreExpresion(e); }

// Archivo del avatar para un estado ("<avatar>@<estado>.mjpeg"); los que falten usan "reposo".
String rutaAvatar(const String &estadoArchivo) {
  if (avatar.isEmpty()) return "";
  if (clips::existe(archivoAvatar(avatar, estadoArchivo))) return archivoAvatar(avatar, estadoArchivo);
  if (clips::existe(archivoAvatar(avatar, "reposo"))) return archivoAvatar(avatar, "reposo");
  return "";
}

// Lee "<avatar>@frases.txt": líneas "estado: frase"; varias por estado se eligen al azar.
void cargarFrases() {
  for (auto &f : frases) f.clear();
  if (avatar.isEmpty()) return;
  File archivo = FFat.open("/" + avatar + "@frases.txt");
  if (!archivo) return;
  while (archivo.available()) {
    String linea = archivo.readStringUntil('\n');
    linea.trim();
    int dos = linea.indexOf(':');
    if (linea.isEmpty() || linea.startsWith("#") || dos < 0) continue;
    String clave = linea.substring(0, dos), frase = linea.substring(dos + 1);
    clave.trim();
    frase.trim();
    Expresion e;
    if (clave == "reposo") clave = "choro";
    if (frase.length() && expresionPorNombre(clave.c_str(), e)) frases[e].push_back(frase);
  }
  archivo.close();
}

String fraseDe(Expresion e) {
  if (frases[e].empty()) return FRASES_NEUTRAS[e];
  return frases[e][random(frases[e].size())];
}

void actualizarCaja() { panel::caja(textoLibre.length() ? textoLibre : fraseActual); }

// Mensaje breve (ej. al tocar la pantalla) que vuelve solo a la frase del estado.
void aviso(const String &texto) {
  textoLibre = texto;
  avisoHasta = millis() + AVISO_MS;
  actualizarCaja();
}

// Usa el clip del avatar activo como cara; si no hay avatar, la cara dibujada.
void cargarSkin() {
  String ruta = rutaAvatar(archivoExpresion(cara.expresion()));
  if (skin.ruta() == ruta && (skin.activo() || ruta.isEmpty())) return;
  bool habia = skin.activo();
  if (ruta.isEmpty() || !skin.abrir(ruta)) skin.cerrar();
  if (habia != skin.activo()) tft.fillRect(L.zona.x, L.zona.y, L.zona.w, L.zona.h, FONDO);
}

void elegirAvatar(const String &nombre) {
  avatar = nombre;
  prefs.putString("avatar", avatar);
  cargarFrases();
  fraseActual = fraseDe(cara.expresion());
  skin.cerrar();
  tft.fillRect(L.zona.x, L.zona.y, L.zona.w, L.zona.h, FONDO);
  cargarSkin();
  actualizarCaja();
}

void siguienteAvatar() {
  auto lista = clips::avatares();
  if (lista.empty()) return;
  size_t i = 0;
  while (i < lista.size() && lista[i] != avatar) i++;
  elegirAvatar(lista[(i + 1) % lista.size()]);
  aviso(avatar);
}

void siguienteFondo() {
  indiceFondo = (indiceFondo + 1) % NUM_COLORES;
  prefs.putChar("fondo", indiceFondo);
  aplicarEstilo();
  aviso(String("Fondo ") + PALETA[indiceFondo].nombre);
}

void cambiarExpresion(Expresion e) {
  cara.setExpresion(e);
  cargarSkin();
  textoLibre = "";
  avisoHasta = 0;
  fraseActual = fraseDe(e);
  actualizarCaja();
}

// Reproduce un clip de evento: primero el del avatar activo, si no, uno suelto de la raíz.
void tocarClip(String nombre) {
  if (nombre.endsWith(".mjpeg")) nombre.remove(nombre.length() - 6);
  if (nombre.isEmpty()) return;
  String ruta = archivoAvatar(avatar, nombre);
  if (avatar.isEmpty() || !clips::existe(ruta)) ruta = nombre + ".mjpeg";
  if (!clips::existe(ruta)) return;
  float fps = clips::reproducir(ruta, L.zona);
  Serial.printf("clip %s fps=%.1f\n", ruta.c_str(), fps);
  if (!skin.activo()) tft.fillRect(L.zona.x, L.zona.y, L.zona.w, L.zona.h, FONDO);
}

// Aplica la orientación guardada y redibuja todo.
void aplicarOrientacion() {
  L = vertical ? VERTICAL : HORIZONTAL;
  tft.setRotation(vertical ? 0 : 1);
  tft.fillScreen(FONDO);
  cara.mover(L.caraX, L.caraY);
  panel::ubicar(L.caja, L.estado);
}

String estadoDemo() {
  String r = red::resumen();
  return r.length() ? "Sin conexión con el PC  " + r : "Sin conexión con el PC";
}

// Guarda un color de la paleta o "#RRGGBB" en `destino` y en NVS; false si no lo reconoce.
bool cambiarColor(const String &arg, uint16_t &destino, const char *clave) {
  uint16_t color;
  if (!parsearColor(arg, color)) return false;
  destino = color;
  prefs.putUShort(clave, color);
  return true;
}

// Comandos por USB o WiFi: "cara <nombre>", "clip <nombre>", "texto [msg]", "estado <msg>",
// "demo on|off", "ping", "girar", "avatar <nombre>", "avatares", "fondo|anillo <color>",
// "recuadro|borde|letras <color>", "circulo on|off"; "wifi ..." solo por USB.
// Sin comandos por SIN_PC_MS vuelve al modo demo.
void procesarComando(String linea, Print &out) {
  linea.trim();
  if (linea.isEmpty()) return;
  ultimoComando = millis();
  int esp = linea.indexOf(' ');
  String cmd = esp < 0 ? linea : linea.substring(0, esp);
  String arg = esp < 0 ? "" : linea.substring(esp + 1);
  Expresion e;
  if (cmd == "cara" && arg == "reposo") arg = "choro";
  if (cmd == "cara" && expresionPorNombre(arg.c_str(), e)) {
    if (e != cara.expresion()) cambiarExpresion(e);
  } else if (cmd == "clip") {
    tocarClip(arg);
  } else if (cmd == "texto") {
    textoLibre = arg;
    avisoHasta = 0;
    actualizarCaja();
  } else if (cmd == "estado") {
    panel::estado(arg);
  } else if (cmd == "demo") {
    modoDemo = arg != "off";
  } else if (cmd == "ping") {
  } else if (cmd == "wifi") {
    red::configurar(arg, out);
    return;
  } else if (cmd == "girar") {
    vertical = arg == "vertical" || (arg != "horizontal" && !vertical);
    prefs.putBool("vertical", vertical);
    aplicarOrientacion();
  } else if (cmd == "avatar") {
    if (!clips::existe(archivoAvatar(arg, "reposo"))) {
      out.println("error avatar");
      return;
    }
    elegirAvatar(arg);
  } else if (cmd == "avatares") {
    for (auto &a : clips::avatares()) out.println(a == avatar ? "* " + a : "  " + a);
  } else if (cmd == "fondo") {
    uint16_t color;
    int indice;
    if (!parsearColor(arg, color, &indice)) {
      out.println("error color");
      return;
    }
    indiceFondo = indice;
    prefs.putChar("fondo", indiceFondo);
    prefs.putUShort("fondo_hex", color);
    aplicarEstilo();
  } else if (cmd == "anillo" || cmd == "recuadro" || cmd == "borde" || cmd == "letras") {
    uint16_t &destino = cmd == "anillo"     ? colorAnillo
                        : cmd == "recuadro" ? colorCaja
                        : cmd == "borde"    ? colorBorde
                                            : colorLetras;
    if (!cambiarColor(arg, destino, cmd.c_str())) {
      out.println("error color");
      return;
    }
    if (cmd == "anillo") aplicarEstilo();
    else aplicarColoresPanel();
  } else if (cmd == "circulo") {
    circulo = arg != "off";
    prefs.putBool("circulo", circulo);
    aplicarEstilo();
    tft.fillRect(L.zona.x, L.zona.y, L.zona.w, L.zona.h, FONDO);
  } else {
    out.println("error comando");
    return;
  }
  out.println("ok");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  randomSeed(esp_random());

  prefs.begin("orion", false);
  vertical = prefs.getBool("vertical", false);
  tft.init();
  indiceFondo = prefs.getChar("fondo", 0);
  if (indiceFondo < -1 || indiceFondo >= NUM_COLORES) indiceFondo = 0;
  colorAnillo = prefs.getUShort("anillo", colorPorNombre("celeste"));
  colorCaja = prefs.getUShort("recuadro", colorPorNombre("pergamino"));
  colorBorde = prefs.getUShort("borde", colorPorNombre("cafe"));
  colorLetras = prefs.getUShort("letras", colorPorNombre("chocolate"));
  circulo = prefs.getBool("circulo", true);
  avatar = prefs.getString("avatar", "");

  if (!cara.begin(0, 0)) Serial.println("sin memoria para la cara");
  if (!clips::begin(tft)) Serial.println("FFat no montado: corre 'pio run -t uploadfs'");
  auto lista = clips::avatares();
  if (!lista.empty() && !clips::existe(archivoAvatar(avatar, "reposo"))) avatar = lista[0];
  cargarFrases();
  fraseActual = fraseDe(cara.expresion());
  aplicarEstilo();

  panel::begin(tft);
  aplicarColoresPanel();
  aplicarOrientacion();
  actualizarCaja();
  panel::estado(estadoDemo());
  cargarSkin();
  red::begin(procesarComando);
  Serial.printf("Orion listo. avatares=%d avatar=%s psram=%u\n", lista.size(), avatar.c_str(), ESP.getFreePsram());
  proximoCambio = millis() + 15000;
}

void loop() {
  while (Serial.available()) procesarComando(Serial.readStringUntil('\n'), Serial);
  red::atender();
  uint32_t ahora = millis();

  bool tocando = hayToque();
  if (tocando && !tocandoAntes) {
    inicioToque = ahora;
    mantenidoAplicado = false;
  }
  if (tocando && !mantenidoAplicado && ahora - inicioToque >= MANTENER_MS) {
    siguienteFondo();
    mantenidoAplicado = true;
  }
  if (!tocando && tocandoAntes && !mantenidoAplicado) siguienteAvatar();
  tocandoAntes = tocando;

  if (avisoHasta && (int32_t)(ahora - avisoHasta) >= 0) {
    avisoHasta = 0;
    textoLibre = "";
    actualizarCaja();
  }

  if (!modoDemo && ahora - ultimoComando > SIN_PC_MS) modoDemo = true;
  if (modoDemo && ahora >= proximoEstadoDemo) {
    proximoEstadoDemo = ahora + 1000;
    panel::estado(estadoDemo());
  }
  if (modoDemo && ahora >= proximoCambio) {
    cambiarExpresion((Expresion)random(NUM_EXPRESIONES));
    proximoCambio = ahora + random(12000, 25000);
  }

  if (ahora >= proximoCuadro) {
    if (skin.activo()) {
      proximoCuadro = ahora + CADA_CUADRO_SKIN_MS;
      skin.cuadro(L.zona);
    } else {
      proximoCuadro = ahora + CADA_CUADRO_MS;
      cara.actualizar(ahora);
    }
  }
}
