# Build High-Performance IoT Communication Gateway

You are a senior C++ systems engineer, Linux networking engineer, performance engineer, and full-stack developer.

Build a complete, technically rigorous, portfolio-quality project called High Performance IoT Communication Gateway, a high-performance IoT communication gateway with an interactive real-time monitoring dashboard, simulated IoT devices, load testing, and Docker-based portability.

The project is intended for a Qualcomm Software Engineer resume. Prioritize the engineering skills relevant to Qualcomm's job description:

- C/C++ and object-oriented design
- Linux operating systems and system programming
- TCP/IP and UDP networking
- Multithreading and synchronization
- Event-driven I/O and scalability
- Real-time communication
- Performance optimization and benchmarking
- IoT connectivity and embedded systems concepts

This must be a genuine C++ systems project, not a Python networking application with a C++ wrapper.

The C++ gateway is the core product. The web UI exists to demonstrate, control, and benchmark it.

## 1. Technology Stack

### Core Gateway

- C++20
- Linux
- POSIX sockets
- epoll
- std::thread, mutex, condition_variable, atomic
- CMake
- GoogleTest

### Device Simulator

- C++20
- Nonblocking TCP sockets
- epoll-based event handling
- Configurable simulated device workloads

### Control Backend

- Python 3.12
- FastAPI
- WebSockets for live dashboard updates
- Pydantic for validation

Python must not implement the gateway's core networking or concurrency.

### Frontend

- React
- TypeScript
- Vite
- Tailwind CSS
- shadcn/ui
- Recharts
- Lucide icons

### Infrastructure

- Docker
- Docker Compose
- Multi-stage builds
- GitHub Actions

No external cloud services, paid APIs, or physical IoT devices should be required.

## 2. System Architecture

Implement four major components.

### Component A: C++ IoT Gateway

The gateway receives TCP connections from simulated IoT devices.

Responsibilities:

- Accept connections
- Parse incoming messages
- Maintain device sessions
- Process telemetry
- Handle disconnections
- Collect performance metrics
- Enforce resource limits
- Support graceful shutdown

Implement two interchangeable server architectures:

**Server A — Baseline**

Thread-per-connection TCP server.

Use it as a simple, correct baseline for comparison.

Implement safe thread lifecycle management and bounded resource limits. Do not allow uncontrolled thread creation.

**Server B — Optimized**

Event-driven TCP server using:

- Nonblocking sockets
- Linux epoll
- epoll_wait
- Edge-triggered or level-triggered I/O with documented design decisions
- Bounded worker thread pool
- Thread-safe work queues
- Backpressure mechanisms
- Efficient connection management

Use an event loop for network I/O and worker threads for meaningful processing tasks.

Do not create one thread per client in the optimized architecture.

Handle partial reads, partial writes, EAGAIN, EOF, EINTR, and connection errors correctly.

Do not assume one recv() call corresponds to one complete message.

## 3. Custom Binary Communication Protocol

Design a lightweight binary protocol for communication between devices and the gateway.

Use length-prefixed messages with a versioned header.

Suggested message fields:

- Protocol version
- Message type
- Payload length
- Device ID
- Sequence number
- Device timestamp
- Sensor payload

Support message types:

- CONNECT / REGISTER
- TELEMETRY
- HEARTBEAT
- ACK
- DISCONNECT

Define:

- Exact field sizes
- Byte order
- Maximum message size
- Framing rules
- Serialization/deserialization
- Malformed-message handling
- Protocol version validation

Avoid unsafe reinterpret_cast-based parsing of untrusted network buffers.

Implement explicit serialization and deserialization.

Support temperature, humidity, battery level, and GPS-style telemetry.

The simulated values are synthetic, not actual physical sensor readings.

## 4. C++ Device Simulator

Build a separate C++ executable that simulates large numbers of IoT devices.

The simulator must support:

- 100 devices
- 500 devices
- 1,000 devices
- 5,000 devices
- Higher counts where system resources permit

Each simulated device should:

1. Connect to the gateway.
2. Register a unique device ID.
3. Send periodic telemetry.
4. Send heartbeats.
5. Receive acknowledgements.
6. Disconnect gracefully or unexpectedly according to the test scenario.

Make these parameters configurable:

- Number of devices
- Messages per second per device
- Message payload size
- Test duration
- Connection ramp-up rate
- Burst traffic
- Random disconnect probability
- Reconnection behavior

Do not create one OS thread per simulated device.

Use event-driven I/O for scalable load generation.

Track simulator-side metrics independently from server-side metrics.

## 5. Thread Pool and Concurrency

Implement a reusable C++ thread pool.

Requirements:

- Fixed configurable worker count
- Bounded task queue
- Mutex and condition variable synchronization
- Graceful shutdown
- Exception handling
- Task completion tracking
- Safe ownership and lifetime management

Implement backpressure when queues reach capacity.

Define whether tasks are rejected, delayed, or connections are temporarily prevented from reading.

Never silently discard telemetry messages.

Expose queue depth, rejected task counts, and processing latency as metrics.

Avoid unnecessary locking in hot paths.

Use RAII and smart pointers appropriately.

Document the concurrency model clearly.

## 6. Real-Time Monitoring and Metrics

Instrument the C++ gateway to expose:

**Connectivity**

- Active connections
- Total accepted connections
- Disconnected clients
- Failed connection attempts
- Reconnection counts

**Performance**

- Messages received per second
- Bytes received per second
- Processing throughput
- Average latency
- P50 latency
- P95 latency
- P99 latency

**Resources**

- CPU usage
- Memory usage
- Worker thread count
- Task queue depth
- Open file descriptor count

**Reliability**

- Invalid messages
- Rejected tasks
- Queue overflow events
- Connection errors
- Missing sequence numbers

Clearly distinguish:

- Network latency
- Application processing latency
- End-to-end round-trip latency

Use a monotonic clock for elapsed-time measurements.

Do not calculate one-way latency using unsynchronized clocks from different processes.

Prefer bounded-memory histograms or interval-based aggregation rather than storing every latency sample indefinitely.

The C++ gateway should expose a local control and metrics interface using a Unix domain socket or another explicitly documented IPC mechanism.

FastAPI will communicate with this interface.

Avoid exposing unrestricted control endpoints publicly.

## 7. Interactive Web Dashboard

Build a polished dark-themed monitoring interface inspired by professional infrastructure observability tools.

The UI should be suitable for a live technical interview demonstration.

### Main Dashboard

Display:

**Top KPI Cards**

- Active devices
- Messages per second
- Throughput
- P95 latency
- CPU usage
- Memory usage

**Real-Time Charts**

- Incoming message rate
- Active connections over time
- P50/P95/P99 latency
- CPU and memory usage
- Worker queue utilization

**Connection Monitor**

- Device ID
- Connection state
- Last heartbeat
- Messages received
- Last sequence number

**System Status**

- Gateway health
- Server mode
- Worker count
- Gateway uptime
- Simulator status

Use WebSockets for live updates.

Update the dashboard approximately once per second without causing excessive browser re-renders.

Provide proper loading, disconnected, and error states.

### Simulation Control Panel

Allow users to configure:

- Device count
- Message rate
- Payload size
- Duration
- Server architecture
- Worker thread count
- Traffic pattern

Buttons:

- Start Simulation
- Stop Simulation
- Reset Metrics
- Run Benchmark

Changing server architecture may require a controlled gateway restart. Implement that safely and communicate it clearly in the UI.

Do not pretend that a running server can switch architectures without interruption if the implementation does not support it.

### Device Visualization

Display a virtual fleet of IoT devices.

Show:

- Connected devices
- Disconnected devices
- Devices experiencing errors
- Latest sensor readings

Use a performant table or aggregated visualization rather than rendering thousands of animated DOM elements.

### Benchmark Dashboard

Provide:

- Baseline vs epoll comparison
- Throughput vs concurrent connections
- P95 latency vs concurrent connections
- CPU usage vs load
- Memory usage vs load
- Worker thread scaling
- Test configuration and environment information

Support exporting benchmark results as CSV or JSON.

All charts must use real measured data.

Never populate benchmark charts with fabricated performance results.

## 8. Benchmarking Methodology

Create a repeatable benchmark suite.

Compare:

1. Thread-per-connection server
2. epoll + worker pool server

Run equivalent workloads using the same simulator.

Benchmark scenarios:

- 100 concurrent connections
- 500 concurrent connections
- 1,000 concurrent connections
- 5,000 concurrent connections, where supported

Test:

- Low message frequency
- High message frequency
- Bursty traffic
- Frequent disconnect/reconnect events
- Large payloads

Include:

- Warm-up phase
- Fixed measurement duration
- Multiple benchmark repetitions
- Median results
- Latency percentiles
- Throughput
- Resource consumption
- Error and loss counts

Do not interpret successful socket connections as proof of successful message processing.

Track sent, acknowledged, rejected, and failed messages separately.

Record whether the load generator itself became CPU-bound or resource-constrained.

Document file descriptor limits, CPU allocation, Docker resource constraints, and host architecture.

Do not claim that epoll is always faster. Explain workload-dependent tradeoffs.

## 9. Fault Injection

Add configurable reliability testing.

Support:

- Abrupt client disconnects
- Partial message transmission
- Malformed message headers
- Delayed acknowledgements
- Bursty traffic
- Slow-reading clients
- Temporary overload

For network delay and packet loss, use Linux traffic control (tc/netem) only in an optional isolated test environment.

Do not require privileged Docker containers for normal operation.

Clearly distinguish application-level message drops from network-level packet loss.

The gateway must remain stable under malformed input and recover gracefully from disconnected clients.

## 10. Backend APIs

Implement a FastAPI control service.

Suggested endpoints:

GET /api/health

GET /api/gateway/status

GET /api/metrics

GET /api/devices

POST /api/simulation/start

POST /api/simulation/stop

POST /api/gateway/configure

POST /api/benchmark/start

GET /api/benchmark/results

WebSocket /ws/metrics

The backend should orchestrate the simulator and gateway processes through a well-defined control interface.

Do not use unsafe shell command construction from user input.

Use explicit parameter validation and safe subprocess lifecycle management.

Prevent simultaneous conflicting simulations or benchmarks.

Ensure a stopped or failed subprocess is detected and reported.

Use appropriate timeouts and bounded buffers.

## 11. Docker Deployment

The complete system must run using:

```bash
docker compose up --build
```

Frontend URL:

[http://localhost:3000](http://localhost:3000)

Use Docker Compose services for:

- gateway
- simulator/control backend
- frontend

You may separate simulator and backend services if it improves lifecycle management.

Use multi-stage builds to compile C++ executables and produce smaller runtime images.

Provide:

- Dockerfiles
- docker-compose.yml
- .dockerignore
- Health checks
- Environment variables
- Named volumes only where necessary

Support Linux amd64 and arm64 container builds.

The optimized server must use Linux epoll inside the container.

The project should run on Docker Desktop for macOS, including Apple Silicon, using Linux containers.

Document that Docker Desktop runs Linux containers inside a VM and that benchmark results may differ from native Linux.

Do not require privileged containers.

Do not expose raw TCP device ports outside localhost unless necessary for the demonstration.

## 12. Repository Structure

Use this structure as a starting point:

```text
pulsegate/
├── gateway/
│   ├── include/
│   ├── src/
│   │   ├── network/
│   │   ├── protocol/
│   │   ├── concurrency/
│   │   ├── telemetry/
│   │   └── metrics/
│   ├── tests/
│   └── CMakeLists.txt
├── simulator/
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
├── backend/
│   ├── app/
│   │   ├── api/
│   │   ├── services/
│   │   └── schemas/
│   └── tests/
├── frontend/
│   ├── src/
│   │   ├── components/
│   │   ├── pages/
│   │   ├── hooks/
│   │   └── services/
│   └── package.json
├── benchmarks/
├── docs/
│   ├── architecture.md
│   ├── protocol.md
│   ├── benchmarking.md
│   └── progress.md
├── .github/
│   └── workflows/
├── docker-compose.yml
├── AGENTS.md
└── README.md
```

Keep the structure modular without creating unnecessary abstractions.

## 13. Testing

Write meaningful automated tests.

### Protocol Tests

- Correct serialization/deserialization
- Fragmented TCP messages
- Multiple messages in one read
- Invalid lengths
- Unsupported protocol versions
- Invalid device IDs

### Networking Tests

- Multiple simultaneous connections
- Abrupt disconnection
- Partial writes
- Slow clients
- Server shutdown
- Connection timeouts

### Concurrency Tests

- Thread pool task execution
- Queue capacity handling
- Graceful shutdown
- Worker exception handling
- Concurrent metrics updates

### Integration Tests

- Simulator connects to gateway
- Telemetry is received correctly
- Metrics are updated
- Dashboard API returns real gateway data
- Simulation can be started and stopped

Use sanitizers where supported.

Run tests after each implementation phase.

## 14. Documentation

Write a professional GitHub README including:

- Project overview
- Architecture diagram
- Technology stack
- Quick start
- Docker instructions
- Screenshots
- Benchmark methodology
- Actual benchmark results
- Limitations
- Engineering tradeoffs

Create ENGINEERING.md explaining:

- How epoll works
- Why nonblocking sockets are used
- Thread-per-connection vs event-driven I/O
- Worker pool design
- Message framing over TCP
- Backpressure
- Race-condition prevention
- Memory ownership
- Connection lifecycle
- Latency measurement
- Performance bottlenecks

Make this documentation useful for Qualcomm technical interview preparation.

## 15. Implementation Phases

Follow this order strictly.

**Phase 1: Networking foundation**

- Define the binary protocol.
- Implement the baseline TCP server.
- Implement a small C++ device client.
- Test connection handling and message framing.

**Phase 2: Optimized server**

- Implement nonblocking sockets.
- Implement epoll event loop.
- Implement worker pool.
- Implement safe connection management.
- Test concurrent clients.

**Phase 3: Device simulator**

- Implement scalable simulated device connections.
- Add configurable telemetry rates.
- Add load generation.
- Add simulator-side metrics.

**Phase 4: Observability**

- Instrument the gateway.
- Implement metrics aggregation.
- Implement control IPC.
- Expose metrics through FastAPI.

**Phase 5: Web dashboard**

- Build the React UI.
- Implement live charts.
- Implement simulation controls.
- Implement device monitoring.
- Implement benchmark visualization.

**Phase 6: Docker**

- Containerize components.
- Configure networking.
- Validate lifecycle and health checks.
- Test complete deployment.

**Phase 7: Benchmarking and hardening**

- Run real performance tests.
- Add fault injection.
- Fix resource leaks.
- Validate shutdown behavior.
- Complete documentation.

## 16. Engineering Rules

1. The core gateway and load generator must be implemented in C++.
2. Use real Linux sockets and epoll, not a high-level networking framework.
3. Do not use Python to simulate the performance-critical gateway.
4. Do not use mock data in the final live dashboard.
5. Do not invent benchmark results.
6. Do not overengineer with Kubernetes, Kafka, Redis, or a database.
7. Avoid unnecessary external dependencies.
8. Handle network errors and resource exhaustion correctly.
9. Use RAII for file descriptors and resources.
10. Do not create unbounded queues or threads.
11. Keep the project understandable and explainable by one developer.
12. Prioritize working end-to-end functionality before advanced optimizations.
13. Maintain AGENTS.md and docs/progress.md as work proceeds.
14. Run tests and fix failures before proceeding.
15. Never silently replace a difficult feature with a mock implementation.
16. If a feature cannot be completed, document it accurately.

## 17. Definition of Done

The project is complete when:

- The baseline TCP server works.
- The optimized epoll server works.
- The simulator can create many concurrent device connections.
- Devices send real messages over TCP.
- The gateway correctly parses and processes telemetry.
- The web dashboard displays live measurements.
- Users can start and stop simulations from the UI.
- Users can compare server architectures.
- Performance charts show measured results.
- Fault scenarios are handled without crashes.
- Docker Compose starts the complete application.
- Automated tests pass.
- Documentation explains the architecture and measured performance.

## 18. Final Deliverables

Provide:

1. Complete working source code.
2. Docker-based one-command startup.
3. Interactive real-time dashboard.
4. C++ gateway and simulator.
5. Automated tests.
6. Benchmark suite.
7. Architecture documentation.
8. Professional README.
9. Interview-ready engineering explanations.
10. Two concise resume bullet suggestions using actual measured results only.

Begin by inspecting the repository and available environment.

Create an implementation plan, then build the project incrementally.

Make sensible technical decisions independently rather than asking unnecessary questions.

Prioritize a functional, measurable, technically credible system over adding excessive features.

Do not stop at scaffolding or an attractive UI. Complete and verify the actual C++ networking system.
