import json
import socket

with socket.socket(socket.AF_UNIX) as s:
    s.settimeout(2)
    s.connect("/run/pulsegate/gateway.sock")
    s.sendall(b"status\n")
    result = b""
    while not result.endswith(b"\n"):
        chunk = s.recv(8192)
        if not chunk:
            raise RuntimeError("control EOF")
        result += chunk
    assert json.loads(result)["running"]
