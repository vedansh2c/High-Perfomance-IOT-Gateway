# Implementation plan and progress

1. Define and test explicit binary framing; build baseline and small client.
2. Add Linux epoll, bounded workers, safe completion delivery and backpressure.
3. Build event-driven fleet simulator with independent ACK and RTT metrics.
4. Add bounded metrics, Unix socket control, validated FastAPI orchestration.
5. Build live React dashboard, simulation controls and measured comparisons.
6. Add multi-stage Docker images, Compose and CI; exercise the full deployment.
7. Run reproducible comparisons, fault tests, and document measurements and limitations.

Environment: initially empty repository, macOS ARM host, Docker Linux ARM available. Native epoll tests run inside Linux containers. No pre-existing project code.

Phases 1–3: implemented explicit big-endian framing, baseline, level-triggered epoll, bounded worker pool and simulator. GoogleTest passed (5 tests). Real Linux integration passed baseline then epoll then 100-device fragmented-send simulator. No sent telemetry lost in that test. Phase 4 implementation: Unix IPC and FastAPI with exclusive process orchestration, validated arguments, bounded output, lifecycle detection and WebSocket snapshots.

Phases 4–6: FastAPI tests passed against actual C++ processes, including health, live metrics, WebSocket, exclusive controls, safe restart, benchmark export/completion/cancellation. React/TypeScript production build passed. Compose deployed with healthy gateway/backend/frontend; smoke passed both server modes through the UI API proxy. Port 3000 was occupied by an existing service, so verification uses FRONTEND_PORT=3001 and matching PUBLIC_ORIGIN. Default remains 3000. Browser start/stop/reset/fleet/responsive checks passed; actual screenshots saved under docs/screenshots.

Phase 7: Linux network tests passed malformed headers, partial sends, TCP half-close, duplicate IDs, 32 simultaneous test clients, abrupt disconnect, delayed ACK overload, idle timeout and FD recovery. Address/undefined sanitizers passed unit and integration tests. A real 24-run, 3-repeat comparison admitted 100/500/1000/5000 devices in both architectures, with 2,326,749 sent telemetry messages all acknowledged. Evidence retained in docs/measurements; methodology and workload-dependent latency/memory tradeoffs recorded in docs/results.md. Further final workload checks and final validation are in progress.

Limitations: native AMD64 not exercised in this ARM64 session; optional tc/netem not run; short benchmark windows and coarse latency histograms do not establish saturation performance. Device UI samples at most 200 records. No independently measured network/one-way latency, persistent storage, TLS, public authentication or persistent per-device error history. Documentation explicitly explains these limits rather than adding mock features.

Final checks after hardening: 8 GoogleTest cases passed, 5 real Linux networking/fault tests passed, 3 FastAPI tests passed on Python 3.12, and 1 browser test passed (17 automated cases total). ASan/UBSan passed all C++ unit and networking cases. Nonzero registration sequences and error-state attribution are integration-tested; missing sensor readings display unavailable until telemetry arrives. Benchmark cancellation while a simulator is actively producing output is tested and its output consumer is shielded from cancellation. Simulator tracks unsent queued abandonment separately from fully sent failures.

Docker Desktop's daemon stopped during concurrent final builds; restarted successfully and redeployed. An AMD64 emulation build was interrupted by that environment failure before compiling; native AMD64 remains unvalidated. ARM64 deployment and tests are verified. A 30-run matrix at 100/500 devices across five traffic scenarios and 1/4 epoll workers is being measured on the final deployment.

Final workload matrix completed: 30 measured runs at 100/500 devices, five scenarios, baseline and epoll with 1/4 workers. 415,155 fully transmitted telemetry frames, 415,133 ACKs, 22 unacknowledged frames from intentional churn, zero fully sent failures in other scenarios. Evidence: docs/measurements/workloads.json and .csv. Non-churn runs reached full requested peak concurrency; churn naturally had lower sampled concurrent sessions. Four live comparison plots and JSON/CSV downloads passed browser verification. Together with the earlier 24 repeated comparisons, 54 actual benchmark runs are retained.

AMD64 follow-up: Linux AMD64 C++ gateway/simulator build and 8 unit tests passed under Docker emulation. Native AMD64 performance remains unvalidated. Combined browser checks found that retained disconnected records could hide a smaller active fleet; snapshot selection now prioritizes connected records, with a unit regression and a browser setup that runs 500 devices before checking a new 100-device fleet.

Final status: all seven phases implemented. 19 automated cases passed (9 C++ unit, 5 TCP/IPC integration, 3 FastAPI, 2 browser). Final ASan/UBSan run passed all 9 C++ unit and 5 networking cases with no sanitizer findings. The connected-fleet regression passes after a real 500-device run followed by 100 devices. All three Compose services are healthy at localhost:3001; simulation is stopped and benchmark exports remain available. Source, deployment, CI, screenshots, 54 measured runs, architecture/protocol documentation, engineering explanations and evidence-based resume bullets are complete. Optional network netem and native AMD64 performance validation are documented as unperformed.

Final verification addendum: both AMD64 baseline and epoll servers passed the C++ small-client registration/telemetry/disconnect check under emulation. TCP reset errors now increment the connection-error counter in both modes and are covered in the final integration run. Final ASan/UBSan rerun passed all 9 unit and 5 networking cases; all Compose health checks passed after the last deployment. No workload remains running.

Branding update: replaced the visible PulseGate name with IoT Gateway across the dashboard, page/API titles, documentation, project/package metadata, CI label and benchmark JSON filename. Production build and both browser tests passed; documentation screenshots refreshed and all Compose services healthy at localhost:3001.
