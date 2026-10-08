import { useEffect, useState } from "react";
import type { Snapshot, Point } from "../services/api";
export function useMetrics() {
  const [snapshot, setSnapshot] = useState<Snapshot>({
    health: "connecting",
    gateway: null,
    simulation: { state: "idle" },
  });
  const [history, setHistory] = useState<Point[]>([]);
  const [connected, setConnected] = useState(false);
  useEffect(() => {
    let stopped = false;
    let timer: ReturnType<typeof setTimeout>;
    let ws: WebSocket;
    const connect = () => {
      ws = new WebSocket(
        `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws/metrics`,
      );
      ws.onopen = () => setConnected(true);
      ws.onmessage = (e) => {
        try {
          const data: Snapshot = JSON.parse(e.data);
          setSnapshot(data);
          const g = data.gateway;
          if (g)
            setHistory((old) => [
              ...old.slice(-89),
              {
                time: new Date().toLocaleTimeString([], { hour12: false }),
                rate: g.messages_per_s,
                active: g.active,
                p50: g.processing_p50_us,
                p95: g.processing_p95_us,
                p99: g.processing_p99_us,
                cpu: g.cpu_percent,
                memory: g.rss_bytes / 1048576,
                queue: g.queue,
              },
            ]);
        } catch {
          setConnected(false);
        }
      };
      ws.onclose = () => {
        setConnected(false);
        if (!stopped) timer = setTimeout(connect, 2000);
      };
      ws.onerror = () => ws.close();
    };
    connect();
    return () => {
      stopped = true;
      clearTimeout(timer);
      ws.close();
    };
  }, []);
  return { snapshot, history, connected };
}
