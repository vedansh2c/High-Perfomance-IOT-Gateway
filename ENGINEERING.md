# Engineering notes for systems interviews

## epoll and nonblocking I/O

`epoll_create1` creates a kernel readiness set, `epoll_ctl` adds/modifies/removes descriptors, and `epoll_wait` returns ready descriptors. It reports readiness, not completed operations or complete protocol messages. A socket can become unreadable before recv is called: EAGAIN is normal. Level-triggered readiness persists while data remains, so this project reads at most one 8 KiB block per callback and returns to the scheduler for fairness. Edge triggering would reduce repeated readiness notifications but require draining to EAGAIN and explicitly scheduling unread work after backpressure; its added complexity is unnecessary here.

Nonblocking sockets keep one slow client from stalling all others. Outgoing writes track an offset; EAGAIN preserves unsent bytes and enables EPOLLOUT. EOF marks the input half closed, drains complete decoded frames and ACKs, and rejects a partial trailing frame. EINTR retries, connection errors close only that session, and idle timeouts eventually reclaim abandoned sockets. Linux epoll lives in a Linux Docker VM on macOS, not in the macOS kernel.

## Thread per connection versus workers

Baseline threads make blocking-style sequencing easy and can have lower latency for small fleets and cheap validation. They cost stacks, scheduler work, kernel threads and repeated locking as fleets grow. A bound on admission and reaping finished threads prevents runaway thread creation. Resource exhaustion still exists: a thread-based design cannot promise 5000 connections on every machine.

Optimized I/O has one owner and a fixed worker pool. Threads do real sensor range validation and payload hashing; the event loop handles framing and session bookkeeping. A worker handoff and eventfd notification can cost more than this small workload saves. This project measures the tradeoff rather than assuming epoll always wins. Larger meaningful CPU tasks (calibration, compression, rules) could make worker scaling more useful, but are not simulated with arbitrary sleeps except explicit fault injection.

## Bounded queues and backpressure

A pool submission checks capacity under one mutex, then notifies one worker. Consumers wait on a condition predicate, so spurious wakeups are safe. Shutdown sets a shared flag, wakes every worker, drains pending tasks and joins threads. Task exceptions are caught and counted. Construction failure joins already created workers before rethrowing.

When submit fails, ownership remains with the connection and EPOLLIN is disabled. Pending writes are also bounded, and reads pause while ACK buffers remain. Eventually kernel receive windows slow producers. Simulator queues/outstanding ACKs are capped at 128 per device and rejected generation opportunities are separately counted. Queue retries are not network packet loss. v1 has no NACK, so invalid sessions close and sent-but-unacknowledged telemetry is reported as failed.

## Framing, memory and races

TCP can fragment a header across many recv calls or combine many frames. An incremental decoder validates version and declared length before extracting explicit fields. Integer shifts and float bit_cast avoid alignment, aliasing and host-endian bugs. It never trusts a length to allocate unbounded memory.

RAII descriptors close exactly once and are move-only. Optimized connections are owned by the loop; workers capture frame values and immutable tokens. A stale token is ignored after disconnect. Workers cannot mutate session buffers or the device registry. Baseline registry writes serialize under a mutex; global hot counters are atomic. This avoids a mutex on every optimized connection-state change. Safe shutdown joins tasks before destroying queues or eventfd. Duplicate registrations cannot steal another live session.

## Latency and measurement

`steady_clock` measures elapsed microseconds. Gateway timing measures dispatch/queue/validation, excluding network transfer and injected ACK delay. Baseline has no worker queue. Simulator telemetry RTT starts after the last frame byte is sent and ends when a full matching ACK is decoded. It excludes earlier simulator scheduling and user-space send queue wait. Neither component measures independent network latency; clocks from different processes are never subtracted. The device timestamp is only echoed.

A fixed 64-bucket power-of-two histogram gives bounded memory at the cost of coarse percentile upper bounds: a 511 µs p95 means the 95th percentile falls in a bucket ending at 511 µs. Averages retain sums and counts. Cumulative histograms reset only when explicitly requested; dashboard rates use interval deltas. Benchmark simulator metrics reset after warm-up. Warm-up is extended to cover connection ramp-up.

## Bottlenecks and next experiments

The event loop scans admitted sessions each turn for dispatch and timeouts; a ready queue and timing wheel would reduce O(fleet size) housekeeping. The registry mutex serializes metric updates and snapshots. Small task handoffs may cost more than validation; batched completions and per-worker interval histograms could reduce contention. Simulator scheduling is single-threaded and can become CPU-bound before the gateway; exported CPU time, throttling and connection errors flag this possibility. CPU saturation detection is a heuristic, not proof of a bottleneck. Benchmark throughput is achieved ACK rate, not an independently determined saturation maximum.

The honest next experiment varies payload, frequency, concurrency, workers, CPU quota, and network delay while recording sent/ACK/failed counts. Native Linux, Docker Desktop and different ARM/x86 hardware should be reported separately. Measurements here are local demonstrations, not Qualcomm hardware claims.
