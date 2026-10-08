"""Drive the measured benchmark API; keep every repetition and export JSON/CSV."""

import argparse
import json
import time
import urllib.request
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--url", default="http://localhost:3000")
parser.add_argument("--counts", default="100,500,1000,5000")
parser.add_argument("--workers", default="1,4")
parser.add_argument("--scenarios", default="low,high,burst,churn,large")
parser.add_argument("--duration", type=int, default=10)
parser.add_argument("--warmup", type=int, default=2)
parser.add_argument("--repetitions", type=int, default=3)
parser.add_argument("--output", default="benchmarks/results/latest.json")
args = parser.parse_args()


def api(path, payload=None):
    request = urllib.request.Request(
        args.url + "/api/" + path,
        data=json.dumps(payload).encode() if payload is not None else None,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        return json.load(response)


config = {
    "counts": [int(n) for n in args.counts.split(",")],
    "workers": [int(n) for n in args.workers.split(",")],
    "scenarios": args.scenarios.split(","),
    "duration": args.duration,
    "warmup": args.warmup,
    "repetitions": args.repetitions,
}
api("benchmark/start", config)
while True:
    time.sleep(2)
    result = api("benchmark/results")
    status = result["status"]
    print(f"{status['state']}: {status['completed']}/{status['total']}", flush=True)
    if status["state"] in ("finished", "failed", "stopped"):
        break
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(result, indent=2) + "\n")
with urllib.request.urlopen(
    args.url + "/api/benchmark/export.csv", timeout=10
) as response:
    path.with_suffix(".csv").write_bytes(response.read())
if status["state"] != "finished":
    raise SystemExit(status)
print("Saved", path)
