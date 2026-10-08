export async function api(path: string, body?: unknown) {
  const r = await fetch(
    "/api/" + path,
    body === undefined
      ? {}
      : {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(body),
        },
  );
  const data = await r.json();
  if (!r.ok)
    throw Error(
      typeof data.detail === "string"
        ? data.detail
        : JSON.stringify(data.detail),
    );
  return data;
}
export interface Device {
  id: number;
  connected: boolean;
  errored: boolean;
  has_telemetry: boolean;
  messages: number;
  sequence: number;
  heartbeat_age_s: number;
  temperature: number;
  humidity: number;
  battery: number;
  latitude: number;
  longitude: number;
}
export interface Gateway {
  mode: string;
  running: boolean;
  uptime_s: number;
  active: number;
  messages: number;
  processed: number;
  bytes: number;
  workers: number;
  queue: number;
  queue_capacity: number;
  messages_per_s: number;
  bytes_per_s: number;
  cpu_percent: number;
  rss_bytes: number;
  processing_avg_us: number;
  processing_p50_us: number;
  processing_p95_us: number;
  processing_p99_us: number;
  invalid: number;
  rejected: number;
  overflows: number;
  missing: number;
  errors: number;
  open_fds: number;
  devices: Device[];
  device_count: number;
}
export interface Snapshot {
  health: string;
  error?: string;
  gateway: Gateway | null;
  timestamp?: number;
  simulation: {
    state: string;
    metrics?: Record<string, number>;
    error?: string;
  };
  benchmark?: {
    state: string;
    completed: number;
    total: number;
    error?: string;
  };
}
export interface Point {
  time: string;
  rate: number;
  active: number;
  p50: number;
  p95: number;
  p99: number;
  cpu: number;
  memory: number;
  queue: number;
}
export interface Result {
  mode: string;
  workers: number;
  devices: number;
  scenario: string;
  repetition: number;
  throughput: number;
  rtt_p95_us: number;
  cpu_percent: number | null;
  rss_bytes: number | null;
  simulator: {
    sent: number;
    acknowledged: number;
    failed: number;
    rejected: number;
  };
  generator_constrained: boolean;
  environment: Record<string, unknown>;
}
