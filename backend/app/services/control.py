import asyncio
import contextlib
import json
import math
import os
import platform
import resource
import statistics
import time
from collections import deque
from datetime import datetime, timezone
from pathlib import Path

from fastapi import HTTPException
from app.schemas.models import Benchmark, GatewayConfig, Simulation
from app.services.ipc import command


def environment():
    def read(path):
        try:
            return Path(path).read_text().strip()
        except OSError:
            return "unavailable"

    return {
        "architecture": platform.machine(),
        "kernel": platform.release(),
        "cpus_visible": os.cpu_count(),
        "fd_limit": resource.getrlimit(resource.RLIMIT_NOFILE)[0],
        "cpu_max": read("/sys/fs/cgroup/cpu.max"),
        "memory_max": read("/sys/fs/cgroup/memory.max"),
        "platform": platform.platform(),
        "latency": "log2 histogram upper bounds; simulator telemetry ACK RTT; no one-way estimate",
    }


class Control:
    def __init__(self):
        self.lock = asyncio.Lock()
        self.process = None
        self.reader_task = None
        self.poll_task = None
        self.benchmark_task = None
        self.simulation = {"state": "idle"}
        self.snapshot = {
            "health": "connecting",
            "gateway": None,
            "simulation": self.simulation,
        }
        self.results = deque(maxlen=500)
        self.samples = deque(maxlen=180)
        self.benchmark = {"state": "idle", "completed": 0, "total": 0}
        self.previous = None

    def busy(self):
        return (self.process is not None and self.process.returncode is None) or (
            self.benchmark_task is not None and not self.benchmark_task.done()
        )

    async def configure(self, config: GatewayConfig):
        current = await command("status")
        expected = config.workers if config.mode == "epoll" else 0
        if (
            current["mode"] == config.mode
            and current["workers"] == expected
            and current["running"]
        ):
            return current
        await command(f"configure {config.mode} {config.workers}")
        for _ in range(100):
            await asyncio.sleep(0.05)
            current = await command("status")
            if (
                current["mode"] == config.mode
                and current["workers"] == expected
                and current["running"]
            ):
                return current
        raise RuntimeError("gateway restart timed out")

    async def launch(self, config: Simulation):
        if config.slow_readers > config.devices:
            raise HTTPException(422, "slow_readers exceeds devices")
        await self.configure(config)
        args = [
            os.getenv("SIMULATOR_BIN", "/usr/local/bin/pulse_simulator"),
            "--host",
            os.getenv("GATEWAY_HOST", "gateway"),
        ]
        for key, value in config.model_dump(exclude={"mode", "workers"}).items():
            args.extend(
                [
                    "--" + key.replace("_", "-"),
                    str(value).lower() if isinstance(value, bool) else str(value),
                ]
            )
        creation = asyncio.create_task(asyncio.create_subprocess_exec(
            *args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            limit=65536,
        ))
        try:
            self.process = await asyncio.shield(creation)
        except asyncio.CancelledError:
            orphan = await creation
            if orphan.returncode is None:
                orphan.kill()
            await orphan.wait()
            raise
        self.simulation = {
            "state": "running",
            "config": config.model_dump(),
            "metrics": None,
            "error": None,
        }
        self.reader_task = asyncio.create_task(
            self.consume(self.process, self.simulation)
        )

    async def consume(self, proc, state):
        async def stderr():
            tail = b""
            while chunk := await proc.stderr.read(1024):
                tail = (tail + chunk)[-2048:]
            return tail.decode(errors="replace")

        errors = asyncio.create_task(stderr())
        try:
            while line := await proc.stdout.readline():
                state["metrics"] = json.loads(line)
            code = await proc.wait()
            state["state"] = "finished" if code == 0 else "failed"
            state["returncode"] = code
            state["error"] = (await errors) or None
        except Exception as exc:
            state["state"] = "failed"
            state["error"] = str(exc)
            if proc.returncode is None:
                proc.kill()
            await proc.wait()
            await errors

    async def stop_process(self):
        if self.process and self.process.returncode is None:
            self.process.terminate()
            try:
                await asyncio.wait_for(self.process.wait(), 4)
            except asyncio.TimeoutError:
                self.process.kill()
                await self.process.wait()
        if self.reader_task:
            await asyncio.shield(self.reader_task)

    async def stop(self):
        if self.benchmark_task and not self.benchmark_task.done():
            self.benchmark_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.benchmark_task
            self.benchmark["state"] = "stopped"
        await self.stop_process()
        self.simulation["state"] = "stopped"

    async def poll(self):
        while True:
            try:
                now = time.monotonic()
                gateway = await command("metrics")
                rates = {"messages_per_s": 0.0, "bytes_per_s": 0.0, "cpu_percent": 0.0}
                if self.previous:
                    then, prior = self.previous
                    dt = now - then
                    if gateway["metrics_epoch"] == prior["metrics_epoch"]:
                        rates = {
                            "messages_per_s": max(
                                0, gateway["messages"] - prior["messages"]
                            )
                            / dt,
                            "bytes_per_s": max(0, gateway["bytes"] - prior["bytes"])
                            / dt,
                            "cpu_percent": max(
                                0, gateway["cpu_seconds"] - prior["cpu_seconds"]
                            )
                            / dt
                            * 100,
                        }
                self.previous = now, gateway
                gateway.update(rates)
                self.snapshot = {
                    "health": "online" if gateway["running"] else "restarting",
                    "gateway": gateway,
                    "simulation": self.simulation,
                    "benchmark": self.benchmark,
                    "timestamp": time.time(),
                }
                self.samples.append((now, dict(gateway)))
            except Exception as exc:
                self.snapshot = {
                    "health": "offline",
                    "error": str(exc),
                    "gateway": None,
                    "simulation": self.simulation,
                    "benchmark": self.benchmark,
                }
            await asyncio.sleep(1)

    async def run_benchmark(self, config: Benchmark):
        try:
            self.results.clear()
            total = (
                len(config.counts)
                * len(config.scenarios)
                * (1 + len(config.workers))
                * config.repetitions
            )
            self.benchmark.update(state="running", total=total, completed=0, error=None)
            for count in config.counts:
                for scenario in config.scenarios:
                    for mode, workers in [("baseline", 4)] + [
                        ("epoll", w) for w in config.workers
                    ]:
                        for repetition in range(config.repetitions):
                            warmup = max(config.warmup, math.ceil(count / 1000) + 1)
                            sim = Simulation(
                                mode=mode,
                                workers=workers,
                                devices=count,
                                duration=config.duration,
                                warmup=warmup,
                                rate=1 if scenario == "low" else 20,
                                pattern="burst" if scenario == "burst" else "steady",
                                payload=4096 if scenario == "large" else 20,
                                disconnect_probability=(
                                    0.1 if scenario == "churn" else 0
                                ),
                            )
                            await command("reset")
                            started = time.monotonic()
                            await self.launch(sim)
                            await asyncio.shield(self.reader_task)
                            if self.simulation["state"] != "finished":
                                raise RuntimeError(
                                    self.simulation.get("error") or "simulator failed"
                                )
                            stats = self.simulation["metrics"]
                            samples = [
                                m
                                for t, m in self.samples
                                if started + warmup
                                <= t
                                <= started + warmup + config.duration
                            ]
                            row = {
                                "mode": mode,
                                "workers": workers if mode == "epoll" else 0,
                                "devices": count,
                                "scenario": scenario,
                                "repetition": repetition + 1,
                                "config": sim.model_dump(),
                                "throughput": stats["acknowledged"] / config.duration,
                                "peak_active": max(
                                    (m["active"] for m in samples), default=0
                                ),
                                "median_active": (
                                    statistics.median(m["active"] for m in samples)
                                    if samples
                                    else None
                                ),
                                "rtt_p95_us": stats["rtt_p95_us"],
                                "cpu_percent": (
                                    statistics.mean(m["cpu_percent"] for m in samples)
                                    if samples
                                    else None
                                ),
                                "rss_bytes": max(
                                    (m["rss_bytes"] for m in samples), default=None
                                ),
                                "processing_p95_us": max(
                                    (m["processing_p95_us"] for m in samples),
                                    default=None,
                                ),
                                "simulator": stats,
                                "gateway": await command("metrics"),
                                "environment": environment(),
                                "timestamp": datetime.now(timezone.utc).isoformat(),
                                "generator_cpu_percent": stats["cpu_seconds"]
                                / (warmup + config.duration)
                                * 100,
                                "generator_constrained": stats["throttled"] > 0
                                or stats["connection_errors"] > 0
                                or stats["cpu_seconds"] / (warmup + config.duration)
                                > 0.9,
                            }
                            self.results.append(row)
                            self.benchmark["completed"] += 1
            self.benchmark["state"] = "finished"
        except asyncio.CancelledError:
            await self.stop_process()
            raise
        except Exception as exc:
            self.benchmark.update(state="failed", error=str(exc))
            await self.stop_process()

    async def close(self):
        await self.stop()
        if self.poll_task:
            self.poll_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.poll_task
