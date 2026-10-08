# IoT Gateway binary protocol v1

TCP is a byte stream. A frame consists of exactly 32 header bytes followed by `payload_length` bytes. All integer fields use network byte order (big endian). No structs are cast over network data.

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | Version: 1 |
| 1 | 1 | Type: REGISTER=1, TELEMETRY=2, HEARTBEAT=3, ACK=4, DISCONNECT=5 |
| 2 | 2 | Reserved: zero |
| 4 | 4 | Payload length, 0..4096 |
| 8 | 8 | Device ID, unsigned and nonzero |
| 16 | 8 | Sequence number, unsigned |
| 24 | 8 | Device monotonic timestamp in microseconds, echoed unchanged in ACK |

Maximum frame size is 4128 bytes. Only TELEMETRY has a payload, of 20..4096 bytes. Its first 20 bytes contain five IEEE-754 float32 values: temperature (°C), humidity (%), battery (%), latitude (degrees), longitude (degrees). Float bit patterns are explicitly converted to/from big-endian integers using `std::bit_cast`. Optional trailing bytes are synthetic workload padding and are included in a diagnostic payload hash; the hash is not a wire integrity check. Values must be finite; humidity and battery are 0..100, latitude -90..90, longitude -180..180.

A connection must REGISTER once, before other messages. Active device IDs cannot be registered twice. Sequence numbers must strictly increase within a session; gaps are counted. Reconnection starts a fresh session with a new registration and sequence space. DISCONNECT terminates a session after its ACK is flushed. Server ACK uses the same device, sequence and timestamp with an empty payload. The server never accepts ACK messages as device requests.

The incremental decoder retains partial headers/bodies and emits multiple complete frames per input. Invalid version, reserved bits, type, ID or length closes the connection and increments invalid input. Invalid sensors or session order also close the connection; there is no NACK in v1. Truncated EOF is invalid. A complete frame followed by TCP write-half-close is processed and acknowledged before closure.

Limits: framing buffer 64 KiB; decoded backlog 512 frames/client; outgoing ACK buffer 64 KiB/client; simulator 128 queued/outstanding frames/device and 64 MiB total serialized output; gateway 6000 sessions by default, configurable up to 10000. These explicit caps protect memory under malformed streams and slow clients. Idle sessions time out after 30 seconds (CLI configurable).

Timestamps across processes are never subtracted. The simulator records its own monotonic send-completion time and ACK-receipt time for telemetry RTT. Gateway processing timing starts when validation is dispatched and excludes artificial ACK delay. The optimized path includes queue wait, while baseline validation runs inline. Pure network and one-way latency are not independently measured.
