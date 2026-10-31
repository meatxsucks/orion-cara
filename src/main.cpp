#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <Wire.h>
#include "cara.h"
#include "clips.h"

TFT_eSPI tft;
Cara cara(tft);
clips::Bucle skin;

const uint16_t FONDO = 0x18E3;
const uint8_t TACTIL_DIR = 0x38;
const uint32_t CADA_CUADRO_MS = 50;
const uint32_t CADA_CUADRO_SKIN_MS = 100;
const int CARA_W = 300, CARA_H = 250;

// Posiciones de cada elemento según la orientación de la pantalla.
struct Layout {
  int ancho, alto, caraX, caraY;
  clips::Area zona;
  int barraY, textoY, estadoY;
  bool estadoEnDosLineas;
};
const Layout HORIZONTAL = {480, 320, 90, 12, {80, 0, 320, 266}, 268, 286, 310, false};
const Layout VERTICAL = {320, 480, 10, 35, {0, 0, 320, 320}, 322, 354, 404, true};
Layout L = HORIZONTAL;
bool vertical = false;
Preferences prefs;
const uint32_t SIN_PC_MS = 30000;

const char *FRASES[] = {"Wena, soy Orion", "Feliz la vida po", "Dejame pensarlo...",
                        "Zzz... despues hablamos", "Chuta, algo se cayo"};

// Colores con nombre para el fondo circular y el anillo.
struct ColorNombrado {
  const char *nombre;
  uint8_t r, g, b;
};
const ColorNombrado PALETA[] = {
    {"azul", 21, 101, 192},  {"celeste", 41, 182, 246}, {"morado", 106, 27, 154},
    {"rosado", 216, 27, 96}, {"rojo", 198, 40, 40},     {"naranja", 239, 108, 0},
    {"amarillo", 249, 168, 37}, {"verde", 46, 125, 50}, {"gris", 84, 110, 122},
    {"negro", 0, 0, 0},      {"blanco", 235, 235, 235},
};
const int NUM_COLORES = sizeof(PALETA) / sizeof(PALETA[0]);
const uint32_t MANTENER_MS = 700;

String avatar;
int indiceFondo = 0;
uint16_t colorAnillo = 0;
bool circulo = true;

bool modoDemo = true;
String textoLibre;
String estado = "modo demo";
uint32_t proximoCambio = 0, proximoCuadro = 0;
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

// La barra tiene dos líneas que se redibujan por separado con relleno, sin borrar (evita parpadeo).
void dibujarTexto() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_YELLOW, FONDO);
  const char *texto = textoLibre.length() ? textoLibre.c_str() : FRASES[cara.expresion()];
  int fuente = tft.textWidth(texto, 4) <= L.ancho - 10 ? 4 : 2;
  tft.setTextPadding(L.ancho - 4);
  tft.drawString(texto, L.ancho / 2, L.textoY, fuente);
  tft.setTextPadding(0);
}

void dibujarEstado() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xAD55, FONDO);
  tft.setTextPadding(L.ancho - 4);
  if (L.estadoEnDosLineas) {
    int corte = -1;
    for (int i = estado.indexOf("  "); i >= 0; i = estado.indexOf("  ", i + 2)) {
      if (corte < 0 || abs(i - (int)estado.length() / 2) < abs(corte - (int)estado.length() / 2)) corte = i;
    }
    String a = corte < 0 ? estado : estado.substring(0, corte);
    String b = corte < 0 ? "" : estado.substring(corte + 2);
    tft.drawString(a, L.ancho / 2, L.estadoY, 2);
    tft.drawString(b, L.ancho / 2, L.estadoY + 22, 2);
  } else {
    tft.drawString(estado, L.ancho / 2, L.estadoY, 2);
  }
  tft.setTextPadding(0);
}

void dibujarBarra() {
  tft.fillRect(0, L.barraY, L.ancho, L.alto - L.barraY, FONDO);
  tft.drawFastHLine(20, L.barraY, L.ancho - 40, 0x4208);
  dibujarTexto();
  dibujarEstado();
}

uint16_t colorDe(const ColorNombrado &c) { return tft.color565(c.r, c.g, c.b); }

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

String archivoAvatar(const String &nombre, const String &estadoArchivo) {
  return nombre + "@" + estadoArchivo + ".mjpeg";
}

// Archivo del avatar para un estado ("<avatar>@<estado>.mjpeg"); los que falten usan "reposo".
String rutaAvatar(const String &estadoArchivo) {
  if (avatar.isEmpty()) return "";
  if (clips::existe(archivoAvatar(avatar, estadoArchivo))) return archivoAvatar(avatar, estadoArchivo);
  if (clips::existe(archivoAvatar(avatar, "reposo"))) return archivoAvatar(avatar, "reposo");
  return "";
}

const char *archivoExpresion(Expresion e) { return e == CHORO ? "reposo" : nombreExpresion(e); }

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
  skin.cerrar();
  tft.fillRect(L.zona.x, L.zona.y, L.zona.w, L.zona.h, FONDO);
  cargarSkin();
}

void siguienteAvatar() {
  auto lista = clips::avatares();
  if (lista.empty()) return;
  size_t i = 0;
  while (i < lista.size() && lista[i] != avatar) i++;
  elegirAvatar(lista[(i + 1) % lista.size()]);
  textoLibre = "Avatar: " + avatar;
  dibujarTexto();
}

void siguienteFondo() {
  indiceFondo = (indiceFondo + 1) % NUM_COLORES;
  prefs.putChar("fondo", indiceFondo);
  aplicarEstilo();
  textoLibre = String("Fondo: ") + PALETA[indiceFondo].nombre;
  dibujarTexto();
}

void cambiarExpresion(Expresion e) {
  cara.setExpresion(e);
  cargarSkin();
  textoLibre = "";
  dibujarTexto();
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
  dibujarBarra();
}

// Comandos por USB: "cara <nombre>", "clip <nombre>", "texto <msg>", "estado <msg>",
// "demo on|off", "girar", "avatar <nombre>", "avatares", "fondo <color>", "anillo <color>",
// "circulo on|off". Sin comandos por SIN_PC_MS vuelve solo al modo demo.
void procesarComando(String linea) {
  linea.trim();
  if (linea.isEmpty()) return;
  ultimoComando = millis();
  int esp = linea.indexOf(' ');
  String cmd = esp < 0 ? linea : linea.substring(0, esp);
  String arg = esp < 0 ? "" : linea.substring(esp + 1);
  Expresion e;
  if (cmd == "cara" && arg == "reposo") arg = "choro";
  if (cmd == "cara" && expresionPorNombre(arg.c_str(), e)) {
    cambiarExpresion(e);
  } else if (cmd == "clip") {
    tocarClip(arg);
  } else if (cmd == "texto") {
    textoLibre = arg;
    dibujarTexto();
  } else if (cmd == "estado") {
    if (arg != estado) {
      estado = arg;
      dibujarEstado();
    }
  } else if (cmd == "demo") {
    modoDemo = arg != "off";
  } else if (cmd == "girar") {
    vertical = arg == "vertical" || (arg != "horizontal" && !vertical);
    prefs.putBool("vertical", vertical);
    aplicarOrientacion();
  } else if (cmd == "avatar") {
    if (!clips::existe(archivoAvatar(arg, "reposo"))) {
      Serial.println("error avatar");
      return;
    }
    elegirAvatar(arg);
  } else if (cmd == "avatares") {
    for (auto &a : clips::avatares()) Serial.println(a == avatar ? "* " + a : "  " + a);
  } else if (cmd == "fondo" || cmd == "anillo") {
    uint16_t color;
    int indice;
    if (!parsearColor(arg, color, &indice)) {
      Serial.println("error color");
      return;
    }
    if (cmd == "fondo") {
      indiceFondo = indice;
      prefs.putChar("fondo", indiceFondo);
      prefs.putUShort("fondo_hex", color);
    } else {
      colorAnillo = color;
      prefs.putUShort("anillo", colorAnillo);
    }
    aplicarEstilo();
  } else if (cmd == "circulo") {
    circulo = arg != "off";
    prefs.putBool("circulo", circulo);
    aplicarEstilo();
    tft.fillRect(L.zona.x, L.zona.y, L.zona.w, L.zona.h, FONDO);
  } else {
    Serial.println("error comando");
    return;
  }
  Serial.println("ok");
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
  colorAnillo = prefs.getUShort("anillo", colorDe(PALETA[1]));
  circulo = prefs.getBool("circulo", true);
  avatar = prefs.getString("avatar", "");

  if (!cara.begin(0, 0)) Serial.println("sin memoria para la cara");
  if (!clips::begin(tft)) Serial.println("FFat no montado: corre 'pio run -t uploadfs'");
  auto lista = clips::avatares();
  if (!lista.empty() && !clips::existe(archivoAvatar(avatar, "reposo"))) avatar = lista[0];
  aplicarEstilo();
  Serial.printf("Orion listo. avatares=%d avatar=%s psram=%u\n", lista.size(), avatar.c_str(), ESP.getFreePsram());

  aplicarOrientacion();
  cargarSkin();
  proximoCambio = millis() + 15000;
}

void loop() {
  uint32_t ahora = millis();

  while (Serial.available()) procesarComando(Serial.readStringUntil('\n'));

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

  if (!modoDemo && ahora - ultimoComando > SIN_PC_MS) {
    modoDemo = true;
    estado = "modo demo";
    dibujarEstado();
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
