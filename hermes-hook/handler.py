"""Hook de Hermes: manda a orion-cara los eventos de turno de los canales elegidos.

Solo envía metadatos (evento, canal, paso, herramientas, si hubo respuesta); nunca el
contenido de los mensajes. Corre dentro del gateway, así que no bloquea ni lanza errores.
"""
import json
import os
import socket
import time

SOCKET = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}", "orion-cara.sock")
CANALES = {c.strip() for c in os.environ.get("ORION_CARA_CANALES", "whatsapp_cloud,whatsapp").split(",") if c.strip()}

_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
_sock.setblocking(False)


def handle(event_type: str, context: dict):
    if context.get("platform") not in CANALES:
        return
    evento = {
        "evento": event_type,
        "canal": context.get("platform"),
        "sesion": context.get("session_id") or "",
        "t": time.time(),
    }
    if event_type == "agent:step":
        evento["paso"] = context.get("iteration", 0)
        evento["herramientas"] = [n for n in (context.get("tool_names") or []) if n][:3]
    elif event_type == "agent:end":
        evento["respondio"] = bool((context.get("response") or "").strip())
    try:
        _sock.sendto(json.dumps(evento).encode(), SOCKET)
    except OSError:
        pass
