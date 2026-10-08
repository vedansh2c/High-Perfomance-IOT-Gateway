import asyncio
import csv
import io
import os
from contextlib import asynccontextmanager
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import Response
from app.schemas.models import Benchmark, GatewayConfig, Simulation
from app.services.control import Control
from app.services.ipc import command

control = Control()


@asynccontextmanager
async def lifespan(app):
    control.poll_task = asyncio.create_task(control.poll())
    yield
    await control.close()


app = FastAPI(title="IoT Gateway control", lifespan=lifespan)


@app.middleware("http")
async def same_origin(request: Request, call_next):
    # Browser mutations must originate from this deployment. No permissive CORS.
    origin = request.headers.get("origin")
    allowed = os.getenv("PUBLIC_ORIGIN", "http://localhost:3000")
    if (
        request.method not in ("GET", "HEAD", "OPTIONS")
        and origin
        and origin != allowed
    ):
        return Response("origin rejected", status_code=403)
    return await call_next(request)


@app.get("/api/health")
async def health():
    try:
        return {"ok": bool((await command("status"))["running"])}
    except Exception as exc:
        raise HTTPException(503, str(exc)) from exc


@app.get("/api/gateway/status")
async def status():
    return await command("status")


@app.get("/api/metrics")
async def metrics():
    return control.snapshot


@app.get("/api/devices")
async def devices():
    raw = await command("metrics")
    return {"devices": raw["devices"], "total": raw["device_count"], "limit": 200}


@app.post("/api/gateway/configure")
async def configure(config: GatewayConfig):
    async with control.lock:
        if control.busy():
            raise HTTPException(
                409, "Stop the simulation or benchmark before restarting the gateway"
            )
        try:
            await control.configure(config)
            return {
                "ok": True,
                "mode": config.mode,
                "note": "Mode changes restart the TCP listener and disconnect clients.",
            }
        except Exception as exc:
            raise HTTPException(503, str(exc)) from exc


@app.post("/api/metrics/reset")
async def reset():
    async with control.lock:
        if control.busy():
            raise HTTPException(409, "Stop the active workload before resetting")
        try:
            return await command("reset")
        except Exception as exc:
            raise HTTPException(409, str(exc)) from exc


@app.post("/api/simulation/start")
async def start(config: Simulation):
    async with control.lock:
        if control.busy():
            raise HTTPException(409, "A workload is already running")
        try:
            await control.launch(config)
        except HTTPException:
            raise
        except Exception as exc:
            raise HTTPException(503, str(exc)) from exc
        return control.simulation


@app.post("/api/simulation/stop")
async def stop():
    async with control.lock:
        await control.stop()
        return {"ok": True}


@app.post("/api/benchmark/start")
async def benchmark(config: Benchmark):
    if any(n not in (100, 500, 1000, 5000) for n in config.counts) or any(
        w < 1 or w > 64 for w in config.workers
    ):
        raise HTTPException(422, "Counts must be 100/500/1000/5000; workers 1..64")
    async with control.lock:
        if control.busy():
            raise HTTPException(409, "A workload is already running")
        control.benchmark = {"state": "starting", "completed": 0, "total": 0}
        control.benchmark_task = asyncio.create_task(control.run_benchmark(config))
        return control.benchmark


@app.get("/api/benchmark/results")
async def results():
    return {"status": control.benchmark, "results": list(control.results)}


@app.get("/api/benchmark/export.csv")
async def export_csv():
    out = io.StringIO()
    keys = [
        "mode",
        "workers",
        "devices",
        "scenario",
        "repetition",
        "throughput",
        "rtt_p95_us",
        "cpu_percent",
        "rss_bytes",
        "generator_cpu_percent",
        "generator_constrained",
    ]
    writer = csv.DictWriter(out, fieldnames=keys, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(control.results)
    return Response(
        out.getvalue(),
        media_type="text/csv",
        headers={"Content-Disposition": "attachment; filename=benchmarks.csv"},
    )


@app.websocket("/ws/metrics")
async def websocket(ws: WebSocket):
    origin = ws.headers.get("origin")
    if origin and origin != os.getenv("PUBLIC_ORIGIN", "http://localhost:3000"):
        await ws.close(code=1008)
        return
    await ws.accept()
    try:
        while True:
            await asyncio.wait_for(ws.send_json(control.snapshot), 3)
            await asyncio.sleep(1)
    except (WebSocketDisconnect, asyncio.TimeoutError, RuntimeError):
        pass
