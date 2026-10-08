# Measured results — 2026-10-08

Actual measurements from Docker Desktop Linux ARM64 (`aarch64`, LinuxKit 7.0.14), 10 visible VM CPUs and ~7.75 GiB VM memory. Gateway allocation: 2 CPUs, 768 MiB; simulator/control: 2 CPUs, 512 MiB. File descriptor soft/hard limits 65536; gateway pids limit 7000. Release GCC 12.2 build. The exported environment fields describe the simulator/control container; gateway limits are set in the checked-in Compose configuration.

Workload: 20 telemetry messages/sec/device, 20-byte payload, steady traffic, ramp 1000 connections/sec, four epoll workers. Warm-up 2/2/2/6 seconds for 100/500/1000/5000 devices; 3-second measurement plus ACK drain; three repetitions. Both modes admitted the entire requested fleet in each run. All 2,326,749 sent telemetry messages across the suite were acknowledged (see exact counts below; total computed from evidence).

| Requested / admitted devices | Mode | Median ACK/s | Median RTT p95 upper bound | Median gateway CPU | Median max RSS | Failed telemetry |
|---:|---|---:|---:|---:|---:|---:|
| 100 | baseline | 1,950.0 | 2.047 ms | 8.7% | 5.2 MiB | 0 |
| 100 | epoll | 1,949.7 | 1.023 ms | 8.8% | 4.1 MiB | 0 |
| 500 | baseline | 9,725.0 | 4.095 ms | 30.4% | 10.9 MiB | 0 |
| 500 | epoll | 9,739.3 | 4.095 ms | 24.3% | 5.4 MiB | 0 |
| 1000 | baseline | 19,503.7 | 4.095 ms | 37.8% | 18.1 MiB | 0 |
| 1000 | epoll | 19,481.0 | 8.191 ms | 35.3% | 6.9 MiB | 0 |
| 5000 | baseline | 97,412.3 | 65.535 ms | 113.8% | 77.8 MiB | 0 |
| 5000 | epoll | 98,932.3 | 262.143 ms | 120.6% | 24.5 MiB | 0 |

CPU percentage treats one fully used core as 100%; allocated capacity is 200%. Peak RSS is sampled about once per second. RTT figures are log2 histogram upper bounds; a 262.143 ms result is a coarse bucket ending there, not an exact measured p95 of 262.143 ms.

At 5000 devices, epoll sustained a median 98,932 ACK/s versus baseline 97,412 ACK/s at this offered workload. It used about 24.5 MiB RSS versus 77.8 MiB baseline (approximately 69% less). Baseline had the lower RTT p95 bucket: 65.535 ms versus epoll 262.143 ms. This is evidence of a throughput/memory/latency tradeoff, not a blanket claim that epoll is faster.

The simulator averaged approximately 57–59% of one CPU for 5000-device epoll runs and 77–79% for baseline, with no throttled generation or connection errors. These are short, non-saturation demonstrations collected before the final timeout, simulator memory-cap and telemetry fingerprint refinements. Allocator caches survive architecture restarts, baseline precedes epoll within each count, other host activity was not controlled, and Docker Desktop adds VM overhead. Repeat longer isolated runs before making strong performance claims.

## Evidence

- [Full repetition metrics and configuration](measurements/comparison.json) (device snapshot arrays omitted; all scalar metrics retained).
- [CSV export](measurements/comparison.csv).
- Commands and definitions: [benchmarking.md](benchmarking.md).

## Resume bullets

- Built a C++20/Linux IoT gateway with epoll, a bounded worker pool, binary TCP framing and backpressure; measured ~99k acknowledged telemetry messages/s across 5,000 simulated devices in Docker ARM64 tests.
- Implemented a C++ fleet simulator, FastAPI controls and live React dashboard; compared server architectures over 24 runs with zero unacknowledged sent telemetry and ~69% lower sampled RSS for epoll at 5,000 devices.

These bullets describe the checked-in demonstration only; include the short measurement window and hardware context when asked.

## Final traffic and worker checks

After final hardening, a separate 30-run matrix exercised 100/500 devices, baseline and epoll with 1/4 workers, and all five traffic scenarios (one repetition, 3-second measurement each). Every run completed. Non-churn runs reached the requested peak concurrency; churn runs had lower sampled concurrency during intentional reconnect gaps. The matrix transmitted 415,155 telemetry messages, acknowledged 415,133, and reported 22 failures, all during intentional disconnect/reconnect fault runs. Other scenarios had zero fully sent telemetry failures. These individual short runs validate workload behavior and are not robust worker-scaling performance estimates.

[Raw scalar metrics and configurations](measurements/workloads.json) · [CSV](measurements/workloads.csv). Live dashboard charts and both download formats were verified in the browser using these measured results.
