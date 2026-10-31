#!/usr/bin/env python3
"""Puente PC -> pantalla: la cara refleja los turnos de Orión en WhatsApp y la barra el estado del PC."""
import argparse
import glob
import json
import os
import socket
import subprocess
import time
import urllib.request

import psutil
import serial

PUERTO = os.environ.get("ORION_CARA_PUERTO", "")
LLM_URL = os.environ.get("ORION_LLM_URL", "http://127.0.0.1:8081")
SERVICIO_LLM = os.environ.get("ORION_SERVICIO_LLM", "llm.service")
SERVICIO_HERMES = os.environ.get("ORION_SERVICIO_HERMES", "hermes-gateway.service")
PUERTO_WEBHOOK = int(os.environ.get("ORION_PUERTO_WEBHOOK", "8090"))
SOCKET = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}", "orion-cara.sock")

INTERVALO = 2.0
SIN_RESPUESTA_TRAS = 300.0
MOSTRAR_RESULTADO = {"ok": 10.0, "sin_respuesta": 30.0, "timeout": 30.0}
NOMBRE_CANAL = {"whatsapp_cloud": "WhatsApp", "whatsapp": "WhatsApp"}

# Clips opcionales que se reproducen al ocurrir un evento (si existen en la placa).
CLIPS_EVENTO = {"despierta": "despierta", "respondio": "listo", "error": "error"}


def buscar_puerto():
    """Usa el puerto configurado o el primer CP210x conectado."""
    if PUERTO:
        return PUERTO
    candidatos = glob.glob("/dev/serial/by-id/*CP210*") + glob.glob("/dev/ttyUSB*")
    return candidatos[0] if candidatos else None


def servicio_activo(nombre):
    r = subprocess.run(["systemctl", "--user", "is-active", nombre], capture_output=True, text=True)
    return r.stdout.strip()


def llm_ocupado():
    """True/False según /slots de llama.cpp; None si no responde."""
    try:
        with urllib.request.urlopen(f"{LLM_URL}/slots", timeout=1.5) as r:
            return any(s.get("is_processing") for s in json.load(r))
    except Exception:
        return None


def webhook_escuchando():
    try:
        with socket.create_connection(("127.0.0.1", PUERTO_WEBHOOK), timeout=0.5):
            return True
    except OSError:
        return False


def leer_gpu():
    """(uso %, VRAM usada GB, VRAM total GB, temp °C) de la primera GPU NVIDIA, o None."""
    try:
        r = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu",
             "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=2)
        uso, usada, total, temp = (float(x) for x in r.stdout.splitlines()[0].split(","))
        return uso, usada / 1024, total / 1024, temp
    except Exception:
        return None


def leer_estado():
    return {
        "llm": servicio_activo(SERVICIO_LLM),
        "hermes": servicio_activo(SERVICIO_HERMES),
        "llm_ocupado": llm_ocupado(),
        "webhook": webhook_escuchando(),
        "cpu": psutil.cpu_percent(),
        "ram": psutil.virtual_memory().percent,
        "gpu": leer_gpu(),
    }


def redondear(valor, paso=5):
    return int(round(valor / paso) * paso)


def linea_estado(e):
    marca = lambda s: "ok" if s == "active" else s
    partes = [f"LLM {marca(e['llm'])}", f"Hermes {marca(e['hermes'])}",
              f"WA {'ok' if e['webhook'] else 'off'}",
              f"CPU {redondear(e['cpu'])}%", f"RAM {redondear(e['ram'], 1)}%"]
    if e["gpu"]:
        uso, usada, total, temp = e["gpu"]
        partes.append(f"GPU {redondear(uso)}% {usada:.1f}/{total:.0f}G {temp:.0f}C")
    return "  ".join(partes)


class Turnos:
    """Sigue los turnos de Orión en los canales que avisa el hook de Hermes."""

    def __init__(self):
        self.activos = {}
        self.resultado = None

    def evento(self, ev):
        sesion = ev.get("sesion") or "?"
        canal = NOMBRE_CANAL.get(ev.get("canal"), ev.get("canal", "?"))
        tipo = ev.get("evento")
        if tipo == "agent:start":
            self.activos[sesion] = {"inicio": time.time(), "canal": canal, "paso": 0, "herramientas": []}
        elif tipo == "agent:step" and sesion in self.activos:
            self.activos[sesion].update(paso=ev.get("paso", 0), herramientas=ev.get("herramientas", []))
        elif tipo == "agent:end":
            self.activos.pop(sesion, None)
            self.resultado = ("ok" if ev.get("respondio") else "sin_respuesta", time.time(), canal)
            return self.resultado[0]
        return None

    def revisar_timeouts(self):
        ahora = time.time()
        for sesion, t in list(self.activos.items()):
            if ahora - t["inicio"] > SIN_RESPUESTA_TRAS:
                del self.activos[sesion]
                self.resultado = ("timeout", ahora, t["canal"])
                return "timeout"
        return None

    def cara(self):
        """(expresión, frase) del turno en curso o del último resultado; None si no hay nada que mostrar."""
        if self.activos:
            t = max(self.activos.values(), key=lambda x: x["inicio"])
            if t["herramientas"]:
                return "pensando", f"{t['canal']}: usando {t['herramientas'][0]}"
            if t["paso"]:
                return "pensando", f"{t['canal']}: pensando (paso {t['paso']})"
            return "pensando", f"{t['canal']}: pensando..."
        if self.resultado:
            tipo, cuando, canal = self.resultado
            if time.time() - cuando < MOSTRAR_RESULTADO[tipo]:
                if tipo == "ok":
                    return "feliz", f"{canal}: respondido!"
                if tipo == "timeout":
                    return "error", f"{canal}: no respondio (se pego)"
                return "error", f"{canal}: no respondio"
        return None


def decidir_cara(e, turnos):
    if e["hermes"] == "failed" or e["llm"] == "failed":
        return "error", "Chuta, se cayo un servicio"
    turno = turnos.cara()
    if turno:
        return turno
    if e["llm"] != "active":
        return "durmiendo", "Modo juego: LLM apagado"
    if e["hermes"] != "active":
        return "durmiendo", "Hermes apagado"
    return "choro", "Wena, soy Orion"


def abrir_serie(puerto):
    """Abre sin tocar DTR/RTS para no reiniciar la placa más de lo necesario."""
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = puerto, 115200, 0.2
    s.dtr = s.rts = False
    s.open()
    time.sleep(2.5)
    s.reset_input_buffer()
    return s


def abrir_socket():
    if os.path.exists(SOCKET):
        os.unlink(SOCKET)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(SOCKET)
    sock.setblocking(False)
    return sock


def correr():
    sock = abrir_socket()
    turnos = Turnos()
    while True:
        puerto = buscar_puerto()
        if not puerto:
            time.sleep(5)
            continue
        try:
            print(f"Conectando a {puerto}", flush=True)
            bucle(abrir_serie(puerto), sock, turnos)
        except (serial.SerialException, OSError) as err:
            print(f"Conexión perdida: {err}. Reintentando...", flush=True)
            time.sleep(5)


def bucle(ser, sock, turnos):
    enviar = lambda cmd: ser.write((cmd + "\n").encode())
    enviar("demo off")
    mostrado = {"cara": None, "texto": None, "estado": None}
    texto_manual_hasta = 0.0
    llm_antes = None
    e = None
    proximo = 0.0

    def mostrar(clave, valor):
        if mostrado[clave] != valor:
            enviar(f"{clave} {valor}")
            mostrado[clave] = valor
            if clave == "cara":
                mostrado["texto"] = None

    while True:
        hubo_evento = False
        try:
            while True:
                datos = sock.recv(1024).decode().strip()
                if datos.startswith("{"):
                    ev = json.loads(datos)
                    print(f"evento {ev.get('evento')} {ev.get('canal')} paso={ev.get('paso', '-')}", flush=True)
                    if turnos.evento(ev) == "ok":
                        enviar(f"clip {CLIPS_EVENTO['respondio']}")
                    hubo_evento = True
                else:
                    if datos.startswith("texto "):
                        texto_manual_hasta = time.time() + 15
                        mostrado["texto"] = datos[6:]
                    enviar(datos)
        except BlockingIOError:
            pass
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass

        ahora = time.time()
        if ahora >= proximo:
            proximo = ahora + INTERVALO
            e = leer_estado()
            if llm_antes not in (None, "active") and e["llm"] == "active":
                enviar(f"clip {CLIPS_EVENTO['despierta']}")
            llm_antes = e["llm"]
            if turnos.revisar_timeouts():
                print("turno sin respuesta (timeout)", flush=True)
            mostrar("estado", linea_estado(e))
            hubo_evento = True

        if hubo_evento and e:
            cara, frase = decidir_cara(e, turnos)
            if cara != mostrado["cara"] and cara == "error":
                enviar(f"clip {CLIPS_EVENTO['error']}")
            mostrar("cara", cara)
            if ahora >= texto_manual_hasta:
                mostrar("texto", frase)

        ser.read(ser.in_waiting or 1)
        time.sleep(0.05)


def mandar(comando):
    """Envía un comando al puente en ejecución (ej. 'texto Hola', 'cara feliz', 'clip demo')."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.sendto(comando.encode(), SOCKET)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="accion", required=True)
    sub.add_parser("correr", help="inicia el puente")
    sub.add_parser("estado", help="muestra el estado leído, sin pantalla")
    e = sub.add_parser("enviar", help="manda un comando a la pantalla")
    e.add_argument("comando", nargs="+")
    s = sub.add_parser("simular", help="simula un turno de WhatsApp (prueba sin mandar mensajes)")
    s.add_argument("resultado", choices=["ok", "sin_respuesta"], nargs="?", default="ok")
    a = p.parse_args()

    if a.accion == "correr":
        correr()
    elif a.accion == "estado":
        e = leer_estado()
        print(json.dumps(e, indent=2), decidir_cara(e, Turnos()), linea_estado(e), sep="\n")
    elif a.accion == "simular":
        base = {"canal": "whatsapp_cloud", "sesion": "simulada"}
        for ev, extra, espera in [("agent:start", {}, 3), ("agent:step", {"paso": 1, "herramientas": ["web_search"]}, 3),
                                  ("agent:end", {"respondio": a.resultado == "ok"}, 0)]:
            mandar(json.dumps({"evento": ev, **base, **extra}))
            time.sleep(espera)
    else:
        mandar(" ".join(a.comando))


if __name__ == "__main__":
    main()
