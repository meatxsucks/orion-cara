#include "red.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

namespace red {
namespace {

const uint32_t TOKEN_MS = 3000;
const uint32_t REINTENTO_MS = 30000;
const size_t MAX_LINEA = 512;

Preferences prefs;
WiFiServer servidor(PUERTO);
WiFiClient cliente;
Manejador manejar = nullptr;

String ssid, clave, token;
String nuevoSsid, nuevaClave, nuevoToken;
bool clienteValido = false;
uint32_t clienteDesde = 0, ultimoIntento = 0;
wl_status_t estadoAntes = WL_IDLE_STATUS;
String buffer;

bool configurado() { return ssid.length() && token.length(); }

// Compara en tiempo constante para no filtrar el token por tiempos de respuesta.
bool tokenCorrecto(const String &dado) {
  if (dado.length() != token.length()) return false;
  uint8_t dif = 0;
  for (size_t i = 0; i < token.length(); i++) dif |= dado[i] ^ token[i];
  return dif == 0;
}

void conectar() {
  WiFi.disconnect(true);
  if (!configurado()) return;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), clave.c_str());
  ultimoIntento = millis();
}

void soltarCliente() {
  if (cliente) cliente.stop();
  clienteValido = false;
  buffer = "";
}

// Avisa por USB cuando cambia la conexión y levanta mDNS al obtener IP.
void revisarConexion() {
  wl_status_t ahora = WiFi.status();
  if (ahora != estadoAntes) {
    if (ahora == WL_CONNECTED) {
      MDNS.end();
      if (MDNS.begin(NOMBRE)) MDNS.addService("orion-cara", "tcp", PUERTO);
      servidor.begin();
      Serial.printf("wifi conectado %s\n", WiFi.localIP().toString().c_str());
    } else if (estadoAntes == WL_CONNECTED) {
      soltarCliente();
      Serial.println("wifi desconectado");
    }
    estadoAntes = ahora;
  }
  if (ahora != WL_CONNECTED && configurado() && millis() - ultimoIntento > REINTENTO_MS) conectar();
}

void leerCliente() {
  if (servidor.hasClient()) {
    soltarCliente();
    cliente = servidor.available();
    clienteDesde = millis();
  }
  if (!cliente) return;
  if (!cliente.connected()) {
    soltarCliente();
    return;
  }
  if (!clienteValido && millis() - clienteDesde > TOKEN_MS) {
    soltarCliente();
    return;
  }
  while (cliente.available()) {
    char c = cliente.read();
    if (c != '\n') {
      if (buffer.length() >= MAX_LINEA) {
        soltarCliente();
        return;
      }
      buffer += c;
      continue;
    }
    String linea = buffer;
    buffer = "";
    linea.trim();
    if (!clienteValido) {
      if (!linea.startsWith("token ") || !tokenCorrecto(linea.substring(6))) {
        cliente.println("error token");
        soltarCliente();
        return;
      }
      clienteValido = true;
      cliente.println("ok");
      continue;
    }
    if (linea.startsWith("wifi")) {
      cliente.println("error wifi solo por USB");
      continue;
    }
    manejar(linea, cliente);
  }
}

}  // namespace

void begin(Manejador manejador) {
  manejar = manejador;
  prefs.begin("wifi", false);
  ssid = prefs.getString("ssid", "");
  clave = prefs.getString("clave", "");
  token = prefs.getString("token", "");
  WiFi.persistent(false);
  conectar();
}

void atender() {
  if (!configurado()) return;
  revisarConexion();
  if (WiFi.status() == WL_CONNECTED) leerCliente();
}

void configurar(const String &arg, Print &out) {
  int esp = arg.indexOf(' ');
  String sub = esp < 0 ? arg : arg.substring(0, esp);
  String valor = esp < 0 ? "" : arg.substring(esp + 1);
  if (sub == "ssid") {
    nuevoSsid = valor;
  } else if (sub == "clave") {
    nuevaClave = valor;
  } else if (sub == "token") {
    nuevoToken = valor;
  } else if (sub == "conectar") {
    if (nuevoSsid.isEmpty() || nuevoToken.length() < 16) {
      out.println("error wifi falta ssid o token (16+ caracteres)");
      return;
    }
    ssid = nuevoSsid;
    clave = nuevaClave;
    token = nuevoToken;
    nuevoSsid = nuevaClave = nuevoToken = "";
    prefs.putString("ssid", ssid);
    prefs.putString("clave", clave);
    prefs.putString("token", token);
    soltarCliente();
    estadoAntes = WL_IDLE_STATUS;
    conectar();
  } else if (sub == "olvidar") {
    prefs.clear();
    ssid = clave = token = "";
    soltarCliente();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else if (sub == "estado") {
    out.println(resumen().length() ? resumen() : "sin configurar");
  } else {
    out.println("error wifi");
    return;
  }
  out.println("ok");
}

String resumen() {
  if (!configurado()) return "";
  if (WiFi.status() == WL_CONNECTED) return "WiFi " + WiFi.localIP().toString();
  return WiFi.status() == WL_NO_SSID_AVAIL ? "sin WiFi (red no vista)" : "WiFi conectando";
}

}  // namespace red
