#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "cara.h"
#include "clips.h"

TFT_eSPI tft;
Cara cara(tft);
clips::Bucle skin;

const uint16_t FONDO = 0x18E3;
const uint8_t TACTIL_DIR = 0x38;
const int CLIP_X = 120, CLIP_Y = 40;
const uint32_t CADA_CUADRO_MS = 50;
const uint32_t CADA_CUADRO_SKIN_MS = 83;
const int SKIN_X = 120, SKIN_Y = 18;
const uint32_t SIN_PC_MS = 30000;

const char *FRASES[] = {"Wena, soy Orion", "Feliz la vida po", "Dejame pensarlo...",
                        "Zzz... despues hablamos", "Chuta, algo se cayo"};

bool modoDemo = true;
String textoLibre;
String estado = "modo demo";
uint32_t proximoCambio = 0, proximoClip = 0, proximoCuadro = 0;
bool tocandoAntes = false;
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
  tft.setTextPadding(470);
  const char *texto = textoLibre.length() ? textoLibre.c_str() : FRASES[cara.expresion()];
  tft.drawString(texto, 240, 286, 4);
  tft.setTextPadding(0);
}

void dibujarEstado() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xAD55, FONDO);
  tft.setTextPadding(476);
  tft.drawString(estado, 240, 310, 2);
  tft.setTextPadding(0);
}

void dibujarBarra() {
  tft.fillRect(0, 268, 480, 52, FONDO);
  tft.drawFastHLine(20, 268, 440, 0x4208);
  dibujarTexto();
  dibujarEstado();
}

// Usa "cara_<expresion>.mjpeg" como skin si existe; si no, la cara dibujada.
void cargarSkin() {
  String archivo = String("cara_") + nombreExpresion(cara.expresion()) + ".mjpeg";
  if (skin.nombre() == archivo) return;
  bool habia = skin.activo();
  if (clips::existe(archivo)) {
    skin.abrir(archivo);
  } else {
    skin.cerrar();
  }
  if (!habia && skin.activo()) {
    tft.fillRect(90, 12, SKIN_X - 90, 250, FONDO);
    tft.fillRect(SKIN_X + 240, 12, 390 - SKIN_X - 240, 250, FONDO);
    tft.fillRect(SKIN_X, 12, 240, SKIN_Y - 12, FONDO);
    tft.fillRect(SKIN_X, SKIN_Y + 240, 240, 262 - SKIN_Y - 240, FONDO);
  }
}

void cambiarExpresion(Expresion e) {
  cara.setExpresion(e);
  cargarSkin();
  textoLibre = "";
  dibujarTexto();
}

void tocarClip(const String &nombre) {
  if (nombre.isEmpty() || !clips::existe(nombre) || nombre.startsWith("cara_")) return;
  tft.fillScreen(TFT_BLACK);
  float fps = clips::reproducir(nombre, CLIP_X, CLIP_Y);
  Serial.printf("clip %s fps=%.1f\n", nombre.c_str(), fps);
  tft.fillScreen(FONDO);
  dibujarBarra();
}

// Comandos por USB: "cara <nombre>", "clip <archivo>", "texto <msg>", "estado <msg>",
// "demo on|off", "lista". Sin comandos por SIN_PC_MS vuelve solo al modo demo.
void procesarComando(String linea) {
  linea.trim();
  if (linea.isEmpty()) return;
  ultimoComando = millis();
  int esp = linea.indexOf(' ');
  String cmd = esp < 0 ? linea : linea.substring(0, esp);
  String arg = esp < 0 ? "" : linea.substring(esp + 1);
  Expresion e;
  if (cmd == "cara" && expresionPorNombre(arg.c_str(), e)) {
    cambiarExpresion(e);
  } else if (cmd == "clip") {
    tocarClip(arg.endsWith(".mjpeg") ? arg : arg + ".mjpeg");
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
  } else if (cmd == "lista") {
    for (int i = 0; i < clips::cantidad(); i++) Serial.println(clips::nombre(i));
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

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(FONDO);

  if (!cara.begin(90, 12)) Serial.println("sin memoria para la cara");
  if (!clips::begin(tft)) Serial.println("FFat no montado: corre 'pio run -t uploadfs'");
  Serial.printf("Orion listo. clips=%d psram=%u\n", clips::cantidad(), ESP.getFreePsram());

  cargarSkin();
  dibujarBarra();
  proximoCambio = millis() + 15000;
  proximoClip = millis() + 45000;
}

void loop() {
  uint32_t ahora = millis();

  while (Serial.available()) procesarComando(Serial.readStringUntil('\n'));

  bool tocando = hayToque();
  if (tocando && !tocandoAntes) {
    cambiarExpresion((Expresion)((cara.expresion() + 1) % NUM_EXPRESIONES));
    proximoCambio = ahora + 20000;
  }
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
  if (modoDemo && ahora >= proximoClip) {
    tocarClip(clips::aleatorio());
    proximoClip = millis() + random(40000, 70000);
  }

  if (ahora >= proximoCuadro) {
    if (skin.activo()) {
      proximoCuadro = ahora + CADA_CUADRO_SKIN_MS;
      skin.cuadro(SKIN_X, SKIN_Y);
    } else {
      proximoCuadro = ahora + CADA_CUADRO_MS;
      cara.actualizar(ahora);
    }
  }
}
