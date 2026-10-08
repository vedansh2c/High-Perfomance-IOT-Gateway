# Reproducible benchmarking

Run Compose first, then:

```bash
# Complete suite: both architectures, all target counts, five traffic scenarios,
# workers 1 and 4, three repetitions; this takes roughly 20 minutes.
python3 benchmarks/run.py

# Short comparison suitable for a demo
python3 benchmarks/run.py --counts 100,500 --workers 4 --scenarios high --duration 5 --repetitions 3

# Use the alternate demo port when 3000 is occupied
python3 benchmarks/run.py --url http://localhost:3001 --counts 100,500,1000,5000 --workers 4 --scenarios high --duration 3
```

Every test uses the same C++ simulator. Low = 1 telemetry/sec/device, high = 20, burst = a 5x rate in 200 ms windows, churn = 20 with 0.1 abrupt-disconnect probability/second and reconnect, large = 4096-byte payload at 20/sec. Synthetic values are deterministic; fault probability generator is seeded. Warm-up is at least `ceil(devices / ramp) + 1` seconds (ramp 1000/sec), then counters/histograms reset in the simulator. Each measurement lasts the requested fixed duration followed by up to two seconds to drain ACKs. All repetitions are exported; dashboard bars use medians.

Throughput = telemetry ACKs / fixed measurement duration, including final ACK drain. Successful connections do not prove processed telemetry. Sent counts increment only after the complete telemetry frame is transmitted. Failed counts cover fully sent telemetry that remained unacknowledged when a session closed. Rejected counts cover generation opportunities not queued due to the 128-frame limit, including protocol control opportunities. Gateway rejected/overflow counts are retries of a full worker queue; they are not telemetry drops. HTTP process failure and partial connection admission must be read alongside throughput.

Server processing percentiles are cumulative since reset and include warm-up/control frames. Simulator RTT percentiles cover only measurement telemetry. These different populations are deliberately labeled; they must not be compared as identical latency metrics. Gateway CPU and max RSS are sampled within the measurement interval, at about one-second cadence. RTT histograms use power-of-two bucket upper bounds. Simulator CPU covers its entire process, divided by warm-up + measurement duration; drain adds a small bias. Generator-constrained is a heuristic (roughly one core of CPU, queue throttling, or connection errors).

Exports include architecture, kernel, visible CPUs, RLIMIT_NOFILE, cgroup CPU/memory limits, workload configuration, repetition and timestamp. Compose allocates gateway 2 CPUs/768 MiB and generator/backend 2 CPUs/512 MiB, with fd limits 65536. Baseline admission cap is 6000 and pids limit 7000; host thread limits can still prevent a complete 5000-device run. Resource-restricted runs stay in the report with their failure counters, never relabeled as successful equal-concurrency results.

These are short engineering demonstrations. Three-second tests are useful for correctness and observing tradeoffs, but need longer (30–120 second) measurement runs and more repetitions for robust performance claims. Worker scaling bars use separate epoll worker series at equal counts. Full JSON/CSV contains test environment; result storage is in memory and replaced when a new suite starts. Export before restarting the backend.

## Fault injection

```bash
# Run a controlled workload with partial sends, malformed register headers and churn
curl -X POST http://localhost:3000/api/simulation/start \
  -H 'Content-Type: application/json' \
  -d '{"devices":100,"duration":10,"rate":30,"partial":3,"malformed":0.1,"disconnect_probability":0.1,"slow_readers":5}'

# Inject delayed ACKs directly in a standalone gateway (Linux)
build/gateway/pulse_gateway --mode epoll --workers 1 --queue 8 --ack-delay-ms 50
```

The simulator exposes burst, partial transmission, abrupt disconnect/reconnect, malformed registration and clients that send telemetry without consuming ACKs. Small worker/queue capacities plus ACK delay produce temporary overload. `scripts/integration.py` verifies recovery and descriptor cleanup. Network-level delay/loss requires an optional isolated container with NET_ADMIN, described in [netem.md](netem.md). Normal deployment has no privileged containers or capabilities. TCP packet loss causes retries, not necessarily application message loss.

Actual checked-in measurements are recorded in [results.md](results.md) with their raw evidence. No benchmark results are bundled into live dashboard state.
