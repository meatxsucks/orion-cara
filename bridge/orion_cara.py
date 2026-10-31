#!/usr/bin/env python3
"""Puente PC -> pantalla: lee el estado de Orión (llama.cpp + Hermes) y lo manda por USB."""
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
SOCKET = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "orion-cara.sock")
INTERVALO = 2.0
FELIZ_TRAS_RESPONDER = 6.0

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


def llm_procesando():
    """True/False según /slots de llama.cpp; None si no responde."""
    try:
        with urllib.request.urlopen(f"{LLM_URL}/slots", timeout=1.5) as r:
            return any(s.get("is_processing") for s in json.load(r))
    except Exception:
        return None


def leer_estado():
    return {
        "llm": servicio_activo(SERVICIO_LLM),
        "hermes": servicio_activo(SERVICIO_HERMES),
        "procesando": llm_procesando(),
        "cpu": psutil.cpu_percent(),
        "ram": psutil.virtual_memory().percent,
    }


def decidir_cara(e, ultima_respuesta):
    """Devuelve (expresión, frase) según el estado."""
    if e["hermes"] == "failed" or e["llm"] == "failed":
        return "error", "Chuta, se cayo un servicio"
    if e["llm"] != "active":
        return "durmiendo", "Modo juego: LLM apagado"
    if e["procesando"] is None:
        return "durmiendo", "Despertando el cerebro..."
    if e["procesando"]:
        return "pensando", "Pensando, espera po..."
    if time.time() - ultima_respuesta < FELIZ_TRAS_RESPONDER:
        return "feliz", "Listo, respondido!"
    return "choro", "Wena, soy Orion"


def linea_estado(e):
    marca = lambda s: "ok" if s == "active" else s
    return (f"LLM {marca(e['llm'])} | Hermes {marca(e['hermes'])} | "
            f"CPU {e['cpu']:.0f}% | RAM {e['ram']:.0f}% | {time.strftime('%H:%M')}")


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
    while True:
        puerto = buscar_puerto()
        if not puerto:
            time.sleep(5)
            continue
        try:
            print(f"Conectando a {puerto}", flush=True)
            ser = abrir_serie(puerto)
            bucle(ser, sock)
        except (serial.SerialException, OSError) as err:
            print(f"Conexión perdida: {err}. Reintentando...", flush=True)
            time.sleep(5)


def bucle(ser, sock):
    enviar = lambda cmd: ser.write((cmd + "\n").encode())
    enviar("demo off")
    cara_actual = None
    texto_manual_hasta = 0.0
    procesando_antes = llm_antes = None
    ultima_respuesta = 0.0
    proximo = 0.0

    while True:
        try:
            while True:
                datos = sock.recv(512).decode().strip()
                if datos.startswith("texto "):
                    texto_manual_hasta = time.time() + 15
                enviar(datos)
        except BlockingIOError:
            pass

        ahora = time.time()
        if ahora >= proximo:
            proximo = ahora + INTERVALO
            e = leer_estado()

            if procesando_antes and e["procesando"] is False:
                ultima_respuesta = ahora
                enviar(f"clip {CLIPS_EVENTO['respondio']}")
            if llm_antes not in (None, "active") and e["llm"] == "active":
                enviar(f"clip {CLIPS_EVENTO['despierta']}")
            procesando_antes, llm_antes = e["procesando"], e["llm"]

            cara, frase = decidir_cara(e, ultima_respuesta)
            if cara != cara_actual:
                if cara == "error":
                    enviar(f"clip {CLIPS_EVENTO['error']}")
                enviar(f"cara {cara}")
                cara_actual = cara
                if ahora >= texto_manual_hasta:
                    enviar(f"texto {frase}")
            enviar(f"estado {linea_estado(e)}")

        ser.read(ser.in_waiting or 1)
        time.sleep(0.1)


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
    a = p.parse_args()

    if a.accion == "correr":
        correr()
    elif a.accion == "estado":
        e = leer_estado()
        print(json.dumps(e, indent=2), decidir_cara(e, 0), linea_estado(e), sep="\n")
    else:
        mandar(" ".join(a.comando))


if __name__ == "__main__":
    main()
