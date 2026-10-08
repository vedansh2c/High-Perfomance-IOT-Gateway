# Optional isolated network fault environment

Normal Compose requires no privilege. To test Linux network delay/loss, use a disposable override that adds only NET_ADMIN to the gateway network namespace. This affects the isolated container, not the host interface. Install iproute2 only in that disposable test image (as root), then run:

```bash
# Inside the isolated test container, not on the host
tc qdisc add dev eth0 root netem delay 20ms 5ms loss 1%
# ... run the same simulator benchmark ...
tc -s qdisc show dev eth0
tc qdisc del dev eth0 root
```

NET_ADMIN is deliberately absent from docker-compose.yml. Do not use --privileged or apply netem to the host interface. TCP retransmissions can hide packet loss at the application layer: compare RTT and ACK completion, and collect `tc -s` output separately. These optional network-fault commands were not run during implementation.
