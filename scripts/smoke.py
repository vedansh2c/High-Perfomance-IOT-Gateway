import os
import json
import time
import urllib.request

BASE = os.getenv("PULSE_URL", "http://localhost:3000")


def api(path, data=None):
    request = urllib.request.Request(
        BASE + "/api/" + path,
        data=json.dumps(data).encode() if data is not None else None,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.load(response)


assert api("health")["ok"]
for mode in ("baseline", "epoll"):
    api("simulation/start", {"mode": mode, "devices": 100, "rate": 10, "duration": 3})
    for _ in range(15):
        time.sleep(1)
        result = api("metrics")
        if result["simulation"]["state"] == "finished":
            break
    sim = result["simulation"]["metrics"]
    assert sim["acknowledged"] > 500, result
    assert sim["failed"] == 0 and sim["sent"] == sim["acknowledged"], sim
    print(mode, sim)
api("metrics/reset", {})
print("Compose smoke passed: UI proxy, API, real TCP, both modes, ACK accounting.")
