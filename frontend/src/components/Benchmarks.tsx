import { useEffect, useState } from "react";
import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from "recharts";
import { Download } from "lucide-react";
import { api, type Result } from "../services/api";
import { Button } from "./ui/button";
const median = (a: number[]) => {
  const b = a.slice().sort((x, y) => x - y);
  return b.length % 2
    ? b[b.length >> 1]
    : (b[b.length / 2 - 1] + b[b.length / 2]) / 2;
};
export function Benchmarks({ state }: { state: string }) {
  const [rows, setRows] = useState<Result[]>([]);
  const [error, setError] = useState("");
  useEffect(() => {
    let active = true;
    const load = () =>
      api("benchmark/results")
        .then((r) => {
          if (active) {
            setRows(r.results);
            setError("");
          }
        })
        .catch((e) => active && setError(String(e)));
    load();
    const id = setInterval(load, 3000);
    return () => {
      active = false;
      clearInterval(id);
    };
  }, [state]);
  const groups = new Map<string, Result[]>();
  for (const row of rows) {
    const key = `${row.devices} / ${row.scenario}`;
    groups.set(key, [...(groups.get(key) || []), row]);
  }
  const modes = [
    ...new Set(
      rows.map((r) =>
        r.mode === "baseline" ? "baseline" : `epoll · ${r.workers}w`,
      ),
    ),
  ];
  const data = [...groups].map(([name, list]) => {
    const result: Record<string, string | number> = { name };
    for (const mode of modes) {
      const filtered = list.filter(
        (r) =>
          (r.mode === "baseline" ? "baseline" : `epoll · ${r.workers}w`) ===
          mode,
      );
      if (filtered.length)
        result[mode] = median(filtered.map((r) => r.throughput));
    }
    return result;
  });
  const metricData = (
    metric: "rtt_p95_us" | "cpu_percent" | "rss_bytes",
    scale: number,
  ) =>
    [...groups].map(([name, list]) => {
      const result: Record<string, string | number> = { name };
      for (const mode of modes) {
        const filtered = list
          .filter(
            (r) =>
              (r.mode === "baseline" ? "baseline" : `epoll · ${r.workers}w`) ===
              mode,
          )
          .map((r) => r[metric])
          .filter((n): n is number => n !== null);
        if (filtered.length) result[mode] = median(filtered) / scale;
      }
      return result;
    });
  const comparisonCharts = [
    { title: "Acknowledged telemetry / sec", data },
    { title: "Round-trip p95 · ms", data: metricData("rtt_p95_us", 1000) },
    {
      title: "Gateway CPU · % of one core",
      data: metricData("cpu_percent", 1),
    },
    { title: "Gateway memory · MiB", data: metricData("rss_bytes", 1048576) },
  ];
  const download = () => {
    const blob = new Blob([JSON.stringify(rows, null, 2)], {
      type: "application/json",
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "iot-gateway-benchmarks.json";
    a.click();
    URL.revokeObjectURL(url);
  };
  return (
    <section className="panel" id="benchmarks">
      <div className="panel-heading">
        <div>
          <h3>Architecture comparison</h3>
          <p>Median acknowledged telemetry / sec · equal workloads</p>
        </div>
        <div className="export">
          <Button
            variant="outline"
            size="sm"
            disabled={!rows.length}
            onClick={download}
          >
            <Download size={13} />
            JSON
          </Button>
          <Button variant="outline" size="sm" asChild>
            <a href="/api/benchmark/export.csv">CSV</a>
          </Button>
        </div>
      </div>
      {error && <p className="error">{error}</p>}
      {!rows.length ? (
        <div className="empty benchmark-empty">
          <div className="empty-icon">↗</div>
          <h3>Your next comparison starts here</h3>
          <p>
            Run a benchmark to collect real throughput, latency, and resource
            data.
          </p>
          <span>No preloaded or synthetic results</span>
        </div>
      ) : (
        <>
          <div className="benchmark-grid">
            {comparisonCharts.map((chart) => (
              <div key={chart.title}>
                <h4 className="benchmark-chart-title">{chart.title}</h4>
                <div className="benchmark-chart">
                  <ResponsiveContainer width="100%" height="100%">
                    <BarChart data={chart.data}>
                      <CartesianGrid stroke="#202b3b" vertical={false} />
                      <XAxis dataKey="name" stroke="#8190a5" fontSize={11} />
                      <YAxis stroke="#8190a5" fontSize={11} />
                      <Tooltip
                        contentStyle={{
                          background: "#111b29",
                          border: "1px solid #334155",
                        }}
                      />
                      <Legend />
                      {modes.map((m, i) => (
                        <Bar
                          key={m}
                          dataKey={m}
                          fill={
                            ["#7c8ca5", "#26d6cf", "#99a0ff", "#efb65d"][i % 4]
                          }
                          radius={[4, 4, 0, 0]}
                        />
                      ))}
                    </BarChart>
                  </ResponsiveContainer>
                </div>
              </div>
            ))}
          </div>
          <div className="table-wrap">
            <table>
              <thead>
                <tr>
                  <th>Architecture</th>
                  <th>Load</th>
                  <th>Repeat</th>
                  <th>ACK / sec</th>
                  <th>RTT p95</th>
                  <th>CPU</th>
                  <th>Memory</th>
                  <th>Sent / ACK / failed</th>
                  <th>Generator</th>
                </tr>
              </thead>
              <tbody>
                {rows.slice(-30).map((r, i) => (
                  <tr key={i}>
                    <td>
                      {r.mode} / {r.workers}w
                    </td>
                    <td>
                      {r.devices} · {r.scenario}
                    </td>
                    <td>{r.repetition}</td>
                    <td>{r.throughput.toFixed(0)}</td>
                    <td>{(r.rtt_p95_us / 1000).toFixed(2)} ms</td>
                    <td>{r.cpu_percent?.toFixed(1) ?? "—"}%</td>
                    <td>
                      {r.rss_bytes
                        ? (r.rss_bytes / 1048576).toFixed(1) + " MiB"
                        : "—"}
                    </td>
                    <td>
                      {r.simulator.sent} / {r.simulator.acknowledged} /{" "}
                      {r.simulator.failed}
                    </td>
                    <td>
                      {r.generator_constrained
                        ? "Constrained"
                        : "Within limits"}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p className="benchmark-note">
            RTT includes TCP transport, queueing and acknowledgement.
            Percentiles use bounded histogram upper bounds. Export includes
            configuration and environment.
          </p>
        </>
      )}
    </section>
  );
}
