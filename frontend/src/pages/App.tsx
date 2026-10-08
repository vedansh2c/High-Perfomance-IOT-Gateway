import { useState } from "react";
import {
  Activity,
  Radio,
  LayoutDashboard,
  SlidersHorizontal,
  FlaskConical,
  Server,
  ArrowUpRight,
  Users,
  Zap,
  Gauge,
  Cpu,
  HardDrive,
  Network,
  Terminal,
  CheckCircle2,
} from "lucide-react";
import { useMetrics } from "../hooks/useMetrics";
import { MetricChart } from "../components/MetricChart";
import { Controls } from "../components/Controls";
import { Benchmarks } from "../components/Benchmarks";
const fmt = (n: number | undefined, d = 0) =>
  n === undefined
    ? "—"
    : n.toLocaleString(undefined, { maximumFractionDigits: d });
export default function App() {
  const { snapshot, history, connected } = useMetrics();
  const [error, setError] = useState("");
  const g = snapshot.gateway;
  const sim = snapshot.simulation;
  const bench = snapshot.benchmark;
  const busy =
    sim.state === "running" ||
    bench?.state === "running" ||
    bench?.state === "starting";
  const online = connected && snapshot.health === "online";
  const cards = [
    {
      label: "Active devices",
      value: fmt(g?.active),
      unit: "connections",
      icon: Users,
      color: "cyan",
    },
    {
      label: "Message rate",
      value: fmt(g?.messages_per_s, 0),
      unit: "messages / sec",
      icon: Zap,
      color: "violet",
    },
    {
      label: "Ingress throughput",
      value: g ? fmt(g.bytes_per_s / 1024, 1) : "—",
      unit: "KiB / sec",
      icon: Network,
      color: "cyan",
    },
    {
      label: "Processing p95",
      value: fmt(g?.processing_p95_us, 0),
      unit: "microseconds · cumulative",
      icon: Gauge,
      color: "orange",
    },
    {
      label: "CPU utilization",
      value: fmt(g?.cpu_percent, 1),
      unit: "% · 100 = one core",
      icon: Cpu,
      color: "violet",
    },
    {
      label: "Memory footprint",
      value: g ? fmt(g.rss_bytes / 1048576, 1) : "—",
      unit: "MiB · resident set",
      icon: HardDrive,
      color: "cyan",
    },
  ];
  return (
    <div className="app-shell">
      <aside>
        <a className="brand" href="#">
          <div className="brand-icon">
            <Activity size={23} />
          </div>
          <span>
            IoT <span className="brand-light">Gateway</span>
            <small>COMMUNICATION GATEWAY</small>
          </span>
        </a>
        <div className="workspace">
          <div className="workspace-icon">
            <Server size={16} />
          </div>
          <div>
            Local environment<small>Linux / TCP</small>
          </div>
          <span className="status-dot" />
        </div>
        <p className="nav-label">WORKSPACE</p>
        <nav>
          <a className="nav-active" href="#overview">
            <LayoutDashboard size={17} />
            Overview<span>01</span>
          </a>
          <a href="#fleet">
            <Radio size={17} />
            Device fleet
          </a>
          <a href="#controls">
            <SlidersHorizontal size={17} />
            Simulation
          </a>
          <a href="#benchmarks">
            <FlaskConical size={17} />
            Benchmarks
          </a>
        </nav>
        <div className="sidebar-bottom">
          <div>
            <Terminal size={16} /> C++20 engine
          </div>
          <p>
            POSIX sockets · Linux epoll
            <br />
            Built to measure. Built to explain.
          </p>
          <span className="tag">v1.0 / local deployment</span>
        </div>
      </aside>
      <main id="overview">
        <header>
          <div className="breadcrumb">
            Workspace <span>/</span> Overview
          </div>
          <div className={"live-badge " + (online ? "" : "offline")}>
            <span className="status-dot" />
            {online
              ? "Live connection"
              : connected
                ? snapshot.health
                : "Connecting dashboard"}
          </div>
        </header>
        <div className="page-title">
          <div>
            <div className="eyebrow">GATEWAY OBSERVABILITY</div>
            <h1>Every connection. Every pulse.</h1>
            <p>
              Real-time visibility into your IoT communication infrastructure.
            </p>
          </div>
          <div className="mode-badge">
            <Server size={16} />
            {g?.mode === "baseline"
              ? "Thread per connection"
              : "epoll + worker pool"}
            <ArrowUpRight size={14} />
          </div>
        </div>
        {(error || snapshot.error || sim.error || bench?.error) && (
          <div className="error" role="alert">
            {error || snapshot.error || sim.error || bench?.error}
          </div>
        )}
        <div className="kpis">
          {cards.map((c) => (
            <div className="panel kpi" key={c.label}>
              <div className="kpi-top">
                <span>{c.label}</span>
                <c.icon className={c.color} size={16} />
              </div>
              <strong>{c.value}</strong>
              <small>{c.unit}</small>
            </div>
          ))}
        </div>
        <div className="status-strip">
          <div>
            <CheckCircle2 size={14} className={online ? "cyan" : "orange"} />
            <span>Engine</span>
            <b>{snapshot.health}</b>
          </div>
          <div>
            <span>Workers</span>
            <b>{g?.workers ?? "—"}</b>
          </div>
          <div>
            <span>Uptime</span>
            <b>
              {g
                ? `${Math.floor(g.uptime_s / 60)}m ${Math.floor(g.uptime_s % 60)}s`
                : "—"}
            </b>
          </div>
          <div>
            <span>Open FDs</span>
            <b>{fmt(g?.open_fds)}</b>
          </div>
          <div>
            <span>Simulator</span>
            <b>{sim.state}</b>
          </div>
          <div>
            <span>Benchmark</span>
            <b>
              {bench?.state ?? "idle"}{" "}
              {bench?.state === "running"
                ? `${bench.completed}/${bench.total}`
                : ""}
            </b>
          </div>
        </div>
        <div className="chart-grid">
          <MetricChart
            title="Incoming message rate"
            subtitle="Messages received per second"
            data={history}
            lines={[{ key: "rate", label: "Messages / sec", color: "#2dd4cf" }]}
          />
          <MetricChart
            title="Connected fleet"
            subtitle="Active TCP sessions over time"
            data={history}
            lines={[{ key: "active", label: "Connections", color: "#a2a6ff" }]}
          />
          <MetricChart
            title="Processing latency"
            subtitle="Queue + sensor validation · cumulative histogram · µs"
            data={history}
            lines={[
              { key: "p50", label: "p50", color: "#2dd4cf" },
              { key: "p95", label: "p95", color: "#a2a6ff" },
              { key: "p99", label: "p99", color: "#efb65d" },
            ]}
          />
          <MetricChart
            title="CPU utilization"
            subtitle="Process CPU · 100% represents one core"
            data={history}
            lines={[{ key: "cpu", label: "CPU %", color: "#efb65d" }]}
          />
          <MetricChart
            title="Resident memory"
            subtitle="Gateway resident set · MiB"
            data={history}
            lines={[{ key: "memory", label: "Memory MiB", color: "#a2a6ff" }]}
          />
          <MetricChart
            title="Worker queue"
            subtitle={`Bounded queue · capacity ${g?.queue_capacity ?? 1024} · full queues pause reads`}
            data={history}
            lines={[{ key: "queue", label: "Tasks", color: "#2dd4cf" }]}
          />
        </div>
        <Controls busy={!!busy} onError={setError} />
        <section className="panel fleet" id="fleet">
          <div className="panel-heading">
            <div>
              <h3>
                Device fleet{" "}
                <span className="count-badge">{fmt(g?.device_count)}</span>
              </h3>
              <p>
                Synthetic sensor readings delivered over real TCP connections ·
                showing up to 200 records
              </p>
            </div>
            <span className="tag">
              <span className="status-dot" />
              LIVE TELEMETRY
            </span>
          </div>
          <div className="table-wrap">
            <table>
              <thead>
                <tr>
                  <th>Device</th>
                  <th>Status</th>
                  <th>Heartbeat age</th>
                  <th>Messages</th>
                  <th>Sequence</th>
                  <th>Temperature</th>
                  <th>Humidity</th>
                  <th>Battery</th>
                  <th>Location</th>
                </tr>
              </thead>
              <tbody>
                {g?.devices.map((d) => (
                  <tr key={d.id}>
                    <td className="device-id">
                      <Radio size={13} />
                      PG-{String(d.id).padStart(5, "0")}
                    </td>
                    <td>
                      <span
                        className={
                          "device-status " +
                          (d.errored
                            ? "device-error"
                            : !d.connected
                              ? "inactive"
                              : "")
                        }
                      >
                        {d.connected
                          ? "Connected"
                          : d.errored
                            ? "Error"
                            : "Disconnected"}
                      </span>
                    </td>
                    <td>{d.heartbeat_age_s.toFixed(1)} s</td>
                    <td>{fmt(d.messages)}</td>
                    <td className="mono">{d.sequence}</td>
                    <td>
                      {d.has_telemetry ? `${d.temperature.toFixed(1)} °C` : "—"}
                    </td>
                    <td>
                      {d.has_telemetry ? `${d.humidity.toFixed(0)}%` : "—"}
                    </td>
                    <td>
                      {d.has_telemetry ? `${d.battery.toFixed(0)}%` : "—"}
                    </td>
                    <td>
                      {d.has_telemetry
                        ? `${d.latitude.toFixed(2)}, ${d.longitude.toFixed(2)}`
                        : "—"}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
            {!g?.devices.length && (
              <div className="empty">
                No devices yet. Start a simulation to populate your fleet.
              </div>
            )}
          </div>
          <div className="fleet-footer">
            <span>
              {fmt(g?.processed)} processed · {fmt(g?.invalid)} invalid ·{" "}
              {fmt(g?.missing)} sequence gaps · {fmt(g?.errors)} connection
              errors
            </span>
            <span>{fmt(g?.rejected)} queue submission retries</span>
          </div>
        </section>
        {sim.metrics && (
          <div className="sim-result">
            <Activity size={16} />
            <span>
              Simulator · sent <b>{fmt(sim.metrics.sent)}</b> · acknowledged{" "}
              <b>{fmt(sim.metrics.acknowledged)}</b> · failed{" "}
              <b>{fmt(sim.metrics.failed)}</b> · rejected{" "}
              <b>{fmt(sim.metrics.rejected)}</b> · ACK RTT p95{" "}
              <b>{fmt(sim.metrics.rtt_p95_us / 1000, 2)} ms</b>
            </span>
          </div>
        )}
        <Benchmarks state={bench?.state ?? "idle"} />
        <footer>
          <span>IoT Gateway / High Performance IoT Communication Gateway</span>
          <span>Measured data. Local infrastructure.</span>
        </footer>
      </main>
    </div>
  );
}
