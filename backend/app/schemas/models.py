from typing import Literal
from pydantic import BaseModel, ConfigDict, Field


class GatewayConfig(BaseModel):
    model_config = ConfigDict(extra="forbid")
    mode: Literal["baseline", "epoll"] = "epoll"
    workers: int = Field(default=4, ge=1, le=64)


class Simulation(GatewayConfig):
    devices: int = Field(default=100, ge=1, le=10000)
    rate: float = Field(default=10, ge=0.1, le=1000, allow_inf_nan=False)
    payload: int = Field(default=20, ge=20, le=4096)
    duration: int = Field(default=30, ge=1, le=3600)
    warmup: int = Field(default=0, ge=0, le=300)
    ramp: int = Field(default=1000, ge=1, le=10000)
    pattern: Literal["steady", "burst"] = "steady"
    disconnect_probability: float = Field(default=0, ge=0, le=1, allow_inf_nan=False)
    reconnect: bool = True
    partial: int = Field(default=0, ge=0, le=4096)
    slow_readers: int = Field(default=0, ge=0, le=10000)
    malformed: float = Field(default=0, ge=0, le=1, allow_inf_nan=False)


class Benchmark(BaseModel):
    model_config = ConfigDict(extra="forbid")
    counts: list[int] = Field(default=[100, 500], min_length=1, max_length=4)
    workers: list[int] = Field(default=[4], min_length=1, max_length=4)
    scenarios: list[Literal["low", "high", "burst", "churn", "large"]] = Field(
        default=["high"], min_length=1, max_length=5
    )
    duration: int = Field(default=5, ge=2, le=120)
    warmup: int = Field(default=2, ge=1, le=30)
    repetitions: int = Field(default=3, ge=1, le=5)
