import { useState } from "react";
import {
  Play,
  Square,
  RotateCcw,
  FlaskConical,
  ChevronDown,
} from "lucide-react";
import { Button } from "./ui/button";
import { api } from "../services/api";
export function Controls({
  busy,
  onError,
}: {
  busy: boolean;
  onError: (s: string) => void;
}) {
  const [config, setConfig] = useState({
    devices: 100,
    rate: 10,
    payload: 20,
    duration: 30,
    mode: "epoll",
    workers: 4,
    pattern: "steady",
    ramp: 1000,
    partial: 0,
    disconnect_probability: 0,
    malformed: 0,
    slow_readers: 0,
    reconnect: true,
  });
  const [pending, setPending] = useState(false);
  const [advanced, setAdvanced] = useState(false);
  const action = async (path: string, body: unknown) => {
    setPending(true);
    onError("");
    try {
      await api(path, body);
    } catch (e) {
      onError(String(e));
    } finally {
      setPending(false);
    }
  };
  const field = (
    label: string,
    key: keyof typeof config,
    min: number,
    max: number,
    step = 1,
  ) => (
    <label>
      {label}
      <input
        aria-label={label}
        type="number"
        min={min}
        max={max}
        step={step}
        value={config[key] as number}
        disabled={busy || pending}
        onChange={(e) =>
          setConfig({ ...config, [key]: Number(e.target.value) })
        }
      />
    </label>
  );
  return (
    <section className="panel" id="controls">
      <div className="panel-heading">
        <div>
          <h3>Simulation control</h3>
          <p>A real C++ fleet. Your workload, your measurements.</p>
        </div>
        <span className="tag">TCP / v1</span>
      </div>
      <div className="control-fields">
        {field("Devices", "devices", 1, 10000)}
        {field("Messages / device / sec", "rate", 0.1, 1000, 0.1)}
        {field("Payload · bytes", "payload", 20, 4096)}
        {field("Duration · seconds", "duration", 1, 3600)}
        <label>
          Server architecture
          <select
            aria-label="Server architecture"
            disabled={busy || pending}
            value={config.mode}
            onChange={(e) => setConfig({ ...config, mode: e.target.value })}
          >
            <option value="epoll">epoll + worker pool</option>
            <option value="baseline">Thread per connection</option>
          </select>
        </label>
        {field("Worker threads", "workers", 1, 64)}
        <label>
          Traffic pattern
          <select
            disabled={busy || pending}
            value={config.pattern}
            onChange={(e) => setConfig({ ...config, pattern: e.target.value })}
          >
            <option value="steady">Steady stream</option>
            <option value="burst">Bursty · 200 ms windows</option>
          </select>
        </label>
        {field("Connections / sec", "ramp", 1, 10000)}
      </div>
      <div className="controls-foot">
        <p>
          Architecture changes restart the gateway and interrupt connections.
        </p>
        <button className="text-button" onClick={() => setAdvanced(!advanced)}>
          Fault injection <ChevronDown size={14} />
        </button>
      </div>
      {advanced && (
        <div className="control-fields fault-fields">
          {field("Partial send · bytes (0 off)", "partial", 0, 4096)}
          {field(
            "Disconnect probability / sec",
            "disconnect_probability",
            0,
            1,
            0.01,
          )}
          {field("Malformed register fraction", "malformed", 0, 1, 0.01)}
          {field("Slow-reading devices", "slow_readers", 0, config.devices)}
        </div>
      )}
      <div className="actions">
        <Button
          disabled={busy || pending}
          onClick={() => action("simulation/start", config)}
        >
          <Play size={14} />
          Start simulation
        </Button>
        <Button
          variant="outline"
          disabled={!busy || pending}
          onClick={() => action("simulation/stop", {})}
        >
          <Square size={14} />
          Stop
        </Button>
        <Button
          variant="outline"
          disabled={busy || pending}
          onClick={() => action("metrics/reset", {})}
        >
          <RotateCcw size={14} />
          Reset metrics
        </Button>
        <Button
          variant="outline"
          disabled={busy || pending}
          onClick={() =>
            action("benchmark/start", {
              counts: [100, 500],
              workers: [1, 4],
              scenarios: ["high"],
              repetitions: 3,
              duration: 5,
              warmup: 2,
            })
          }
        >
          <FlaskConical size={14} />
          Run benchmark
        </Button>
      </div>
      <p className="benchmark-note">
        Benchmark: both architectures · 100 / 500 devices · 1 / 4 workers · 3
        repeats · ~2 minutes
      </p>
    </section>
  );
}
