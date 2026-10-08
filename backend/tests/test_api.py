import asyncio
import pytest
from fastapi.testclient import TestClient
from app.main import app, control
from app.schemas.models import Simulation
from pydantic import ValidationError


def test_validation():
    for invalid in [
        {"devices": 0},
        {"rate": float("nan")},
        {"payload": 19},
        {"mode": "shell"},
        {"unknown": 1},
    ]:
        with pytest.raises(ValidationError):
            Simulation(**invalid)


def test_health_metrics_and_process_lifecycle():
    with TestClient(app) as client:
        assert client.get("/api/health").json()["ok"]
        r = client.post(
            "/api/simulation/start", json={"devices": 10, "rate": 5, "duration": 20}
        )
        assert r.status_code == 200, r.text
        assert client.post("/api/simulation/start", json={}).status_code == 409
        assert (
            client.post("/api/gateway/configure", json={"mode": "baseline"}).status_code
            == 409
        )
        import time

        time.sleep(1.2)
        assert client.get("/api/metrics").json()["gateway"]["active"] == 10
        assert client.get("/api/devices").json()["total"] >= 10
        with client.websocket_connect("/ws/metrics") as ws:
            assert ws.receive_json()["health"] == "online"
        assert client.post("/api/simulation/stop").status_code == 200
        assert (
            client.post("/api/gateway/configure", json={"mode": "baseline"}).status_code
            == 200
        )
        assert (
            client.post(
                "/api/gateway/configure", json={"mode": "epoll", "workers": 2}
            ).status_code
            == 200
        )
        assert client.post("/api/metrics/reset").status_code == 200
        assert (
            client.post(
                "/api/simulation/start",
                json={},
                headers={"Origin": "https://evil.example"},
            ).status_code
            == 403
        )


def test_measured_benchmark_and_cancellation():
    import time

    with TestClient(app) as client:
        body = {
            "counts": [100],
            "workers": [1],
            "scenarios": ["low"],
            "duration": 2,
            "warmup": 1,
            "repetitions": 1,
        }
        assert client.post("/api/benchmark/start", json=body).status_code == 200
        assert client.post("/api/simulation/start", json={}).status_code == 409
        for _ in range(40):
            time.sleep(0.3)
            result = client.get("/api/benchmark/results").json()
            if result["status"]["state"] in ("finished", "failed"):
                break
        assert result["status"]["state"] == "finished", result
        assert len(result["results"]) == 2
        for row in result["results"]:
            assert row["simulator"]["acknowledged"] >= 100
            assert row["simulator"]["failed"] == 0
            assert row["peak_active"] == 100
            assert row["environment"]["architecture"]
        assert "baseline" in client.get("/api/benchmark/export.csv").text
        assert client.post("/api/benchmark/start", json=body).status_code == 200
        for _ in range(50):
            if control.process and control.process.returncode is None:
                break
            time.sleep(.02)
        time.sleep(.2)  # Exercise cancellation while awaiting the active output consumer.
        assert client.post("/api/simulation/stop").status_code == 200
        assert control.benchmark["state"] == "stopped"
        assert control.process is None or control.process.returncode is not None
