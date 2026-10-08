import os
import subprocess
import time

p = subprocess.Popen(
    ["/src/build/gateway/pulse_gateway", "--ipc", "/tmp/pulse-test.sock"]
)
try:
    time.sleep(0.2)
    subprocess.run(["pytest", "-q", "backend/tests"], check=True)
finally:
    p.terminate()
    p.wait(timeout=5)
