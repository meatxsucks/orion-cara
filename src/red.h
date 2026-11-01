#pragma once
#include <Arduino.h>

// Conexión WiFi: el puente del PC se conecta por TCP al puerto PUERTO y manda las mismas
// líneas que por USB, precedidas de "token <token>". Los datos se guardan en NVS ("wifi").
namespace red {

const uint16_t PUERTO = 7777;
const char *const NOMBRE = "orion-cara";

using Manejador = void (*)(String linea, Print &out);

void begin(Manejador manejador);

// Atiende la conexión entrante; llamar en cada loop().
void atender();

// Comando "wifi ..." recibido por USB: ssid, clave, token, conectar, olvidar, estado.
void configurar(const String &arg, Print &out);

// Texto corto para la barra: "WiFi 192.168.1.20", "WiFi conectando", "sin WiFi" o "".
String resumen();

}  // namespace red
