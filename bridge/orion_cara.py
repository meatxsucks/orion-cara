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

CONFIG = os.path.expanduser("~/.config/orion-cara/env")


def cargar_config():
    """Carga ~/.config/orion-cara/env (KEY=valor) sin pisar variables ya definidas."""
    try:
        with open(CONFIG) as f:
            for linea in f:
                clave, _, valor = linea.strip().partition("=")
                if clave and not clave.startswith("#") and valor:
                    os.environ.setdefault(clave, valor)
    except FileNotFoundError:
        pass


cargar_config()
PUERTO = os.environ.get("ORION_CARA_PUERTO", "")
HOST = os.environ.get("ORION_CARA_HOST", "orion-cara.local")
PUERTO_RED = int(os.environ.get("ORION_CARA_PUERTO_RED", "7777"))
TOKEN = os.environ.get("ORION_CARA_TOKEN", "")
PING_CADA = 10.0
LLM_URL = os.environ.get("ORION_LLM_URL", "http://127.0.0.1:8081")
SERVICIO_LLM = os.environ.get("ORION_SERVICIO_LLM", "llm.service")
SERVICIO_HERMES = os.environ.get("ORION_SERVICIO_HERMES", "hermes-gateway.service")
PUERTO_WEBHOOK = int(os.environ.get("ORION_PUERTO_WEBHOOK", "8090"))
SOCKET = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}", "orion-cara.sock")

INTERVALO = 2.0
SIN_ACTIVIDAD_TRAS = 120.0
TURNO_MAX = 1200.0
TEXTO_MANUAL = 15.0
MOSTRAR_RESULTADO = {"ok": 10.0, "sin_respuesta": 30.0, "timeout": 30.0}

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
        tipo = ev.get("evento")
        ahora = time.time()
        if tipo == "agent:start":
            self.activos[sesion] = {"inicio": ahora, "actividad": ahora}
        elif tipo == "agent:step" and sesion in self.activos:
            self.activos[sesion]["actividad"] = ahora
        elif tipo == "agent:end":
            self.activos.pop(sesion, None)
            self.resultado = ("ok" if ev.get("respondio") else "sin_respuesta", ahora)
            return self.resultado[0]
        return None

    def revisar_timeouts(self, llm_ocupado):
        """Da por perdido un turno si el LLM lleva SIN_ACTIVIDAD_TRAS sin trabajar o pasó TURNO_MAX."""
        ahora = time.time()
        for sesion, t in list(self.activos.items()):
            if llm_ocupado:
                t["actividad"] = ahora
            if ahora - t["actividad"] > SIN_ACTIVIDAD_TRAS or ahora - t["inicio"] > TURNO_MAX:
                del self.activos[sesion]
                self.resultado = ("timeout", ahora)
                return "timeout"
        return None

    def cara(self):
        """Expresión del turno en curso o del último resultado; None si no hay nada que mostrar."""
        if self.activos:
            return "pensando"
        if self.resultado:
            tipo, cuando = self.resultado
            if time.time() - cuando < MOSTRAR_RESULTADO[tipo]:
                return "feliz" if tipo == "ok" else "error"
        return None


def decidir_cara(e, turnos):
    """Prioridad: servicio caído > turno en curso o reciente > LLM o Hermes apagado > reposo."""
    if e["hermes"] == "failed" or e["llm"] == "failed":
        return "error"
    turno = turnos.cara()
    if turno:
        return turno
    if e["llm"] != "active" or e["hermes"] != "active":
        return "durmiendo"
    return "reposo"


def abrir_serie(puerto):
    """Abre sin tocar DTR/RTS para no reiniciar la placa más de lo necesario."""
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = puerto, 115200, 0.2
    s.dtr = s.rts = False
    s.open()
    time.sleep(2.5)
    s.reset_input_buffer()
    return s


def resolver(host):
    """IP del host; usa avahi si el sistema no resuelve nombres .local."""
    try:
        return socket.gethostbyname(host)
    except OSError:
        pass
    if host.endswith(".local"):
        try:
            r = subprocess.run(["avahi-resolve", "-4", "-n", host], capture_output=True, text=True, timeout=5)
            partes = r.stdout.split()
            if len(partes) == 2:
                return partes[1]
        except (OSError, subprocess.TimeoutExpired):
            pass
    return None


class Serie:
    def __init__(self, puerto):
        self.nombre = "USB"
        self.ser = abrir_serie(puerto)

    def enviar(self, cmd):
        self.ser.write((cmd + "\n").encode())

    def drenar(self):
        """Devuelve True si la placa avisó que se reinició."""
        self.resto = getattr(self, "resto", b"") + self.ser.read(self.ser.in_waiting or 1)
        reinicio = False
        while b"\n" in self.resto:
            linea, self.resto = self.resto.split(b"\n", 1)
            texto = linea.decode(errors="replace").strip()
            if texto and texto != "ok":
                print(f"placa: {texto}", flush=True)
            reinicio = reinicio or texto.startswith("Orion listo")
        return reinicio


class Red:
    def __init__(self, ip):
        self.nombre = "WiFi"
        self.sock = socket.create_connection((ip, PUERTO_RED), timeout=3)
        self.sock.sendall(f"token {TOKEN}\n".encode())
        respuesta = self.sock.recv(64).decode(errors="replace").strip()
        if respuesta != "ok":
            self.sock.close()
            raise OSError(f"la placa rechazó la conexión ({respuesta or 'sin respuesta'})")
        self.sock.setblocking(False)

    def enviar(self, cmd):
        self.sock.sendall((cmd + "\n").encode())

    def drenar(self):
        try:
            if not self.sock.recv(4096):
                raise OSError("la placa cerró la conexión")
        except BlockingIOError:
            pass
        return False


def abrir_conexion():
    """USB si la placa está enchufada al PC; si no, WiFi cuando hay token configurado."""
    puerto = buscar_puerto()
    if puerto:
        print(f"Conectando por USB a {puerto}", flush=True)
        return Serie(puerto)
    if TOKEN:
        ip = resolver(HOST)
        if ip:
            print(f"Conectando por WiFi a {HOST} ({ip})", flush=True)
            return Red(ip)
    return None


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
        try:
            conexion = abrir_conexion()
            if not conexion:
                time.sleep(5)
                continue
            bucle(conexion, sock, turnos)
        except (serial.SerialException, OSError) as err:
            print(f"Conexión perdida: {err}. Reintentando...", flush=True)
            time.sleep(5)


def bucle(conexion, sock, turnos):
    enviar = conexion.enviar
    enviar("demo off")
    mostrado = {"cara": None, "estado": None}
    texto_manual_hasta = 0.0
    llm_antes = None
    e = None
    proximo = ultimo_ping = 0.0

    def mostrar(clave, valor):
        if mostrado[clave] != valor:
            enviar(f"{clave} {valor}")
            mostrado[clave] = valor

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
                elif datos:
                    if datos.startswith("texto "):
                        texto_manual_hasta = time.time() + TEXTO_MANUAL
                    enviar(datos)
        except BlockingIOError:
            pass
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass

        ahora = time.time()
        if texto_manual_hasta and ahora >= texto_manual_hasta:
            texto_manual_hasta = 0.0
            enviar("texto")

        if ahora >= proximo:
            proximo = ahora + INTERVALO
            e = leer_estado()
            if llm_antes not in (None, "active") and e["llm"] == "active":
                enviar(f"clip {CLIPS_EVENTO['despierta']}")
            llm_antes = e["llm"]
            if turnos.revisar_timeouts(e["llm_ocupado"]):
                print("turno sin respuesta (timeout)", flush=True)
            mostrar("estado", f"{conexion.nombre}  {linea_estado(e)}")
            if ahora - ultimo_ping > PING_CADA:
                enviar("demo off")
                for clave, valor in mostrado.items():
                    if valor is not None:
                        enviar(f"{clave} {valor}")
                ultimo_ping = ahora
            hubo_evento = True

        if hubo_evento and e:
            cara = decidir_cara(e, turnos)
            if cara != mostrado["cara"] and cara == "error":
                enviar(f"clip {CLIPS_EVENTO['error']}")
            mostrar("cara", cara)

        if conexion.drenar():
            print("La placa se reinició; reenviando estado", flush=True)
            enviar("demo off")
            mostrado.update(cara=None, estado=None)
            proximo = 0.0
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
