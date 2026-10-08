"""Real TCP/Unix IPC integration checks; runs inside the Linux test image."""

import json
import os
import socket
import struct
import subprocess
import time
import unittest
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATEWAY = os.getenv("GATEWAY_BIN", ROOT + "/build/gateway/pulse_gateway")
SIMULATOR = os.getenv("SIMULATOR_BIN", ROOT + "/build/simulator/pulse_simulator")
HEADER = struct.Struct("!BBHIQQQ")


def frame(kind, device, seq, payload=b""):
    return HEADER.pack(1, kind, 0, len(payload), device, seq, 0) + payload


def ipc(command="metrics"):
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(2)
        s.connect("/tmp/pulse-test.sock")
        s.sendall((command + "\n").encode())
        data = b""
        while not data.endswith(b"\n"):
            part = s.recv(8192)
            if not part:
                raise RuntimeError("IPC EOF")
            data += part
        return json.loads(data)


def ack(s, count=1):
    data = b""
    while len(data) < count * 32:
        part = s.recv(count * 32 - len(data))
        if not part:
            raise RuntimeError("ACK EOF")
        data += part
    assert all(HEADER.unpack(data[i : i + 32])[1] == 4 for i in range(0, len(data), 32))


class Networking(unittest.TestCase):
    def exercise(self, mode):
        p = subprocess.Popen(
            [
                GATEWAY,
                "--mode",
                mode,
                "--ipc",
                "/tmp/pulse-test.sock",
                "--queue",
                "1",
                "--max-connections",
                "128",
            ]
        )
        try:
            for _ in range(100):
                try:
                    if ipc()["running"]:
                        break
                except OSError:
                    pass
                time.sleep(0.03)

            def device(i):
                with socket.create_connection(("127.0.0.1", 9000), timeout=5) as s:
                    b = frame(1, i, 0)
                    for v in b:
                        s.sendall(bytes([v]))
                    ack(s)
                    payload = struct.pack("!fffff", 22, 45, 90, 12, 77)
                    s.sendall(
                        b"".join(frame(2, i, seq, payload) for seq in range(1, 21))
                    )
                    ack(s, 20)
                    s.sendall(frame(5, i, 21))
                    ack(s)

            with ThreadPoolExecutor(max_workers=32) as pool:
                list(pool.map(device, range(1, 65)))
            # Peer half-close preserves complete queued frames and ACKs.
            with socket.create_connection(("127.0.0.1", 9000), timeout=2) as s:
                s.sendall(frame(1, 7777, 0) + frame(3, 7777, 1))
                s.shutdown(socket.SHUT_WR)
                ack(s, 2)
                self.assertEqual(s.recv(1), b"")
            with socket.create_connection(('127.0.0.1', 9000), timeout=2) as s:
                s.sendall(frame(1, 7778, 50) + frame(3, 7778, 51))
                ack(s, 2)
            with socket.create_connection(('127.0.0.1', 9000), timeout=2) as s:
                s.sendall(frame(1, 7779, 0)); ack(s)
                s.sendall(frame(2, 7779, 1, struct.pack('!fffff', 22, 200, 90, 12, 77)))
                self.assertEqual(s.recv(1), b'')
            self.assertTrue(next(d for d in ipc()['devices'] if d['id'] == 7779)['errored'])
            # Bad header and abrupt EOF must not crash the gateway.
            with socket.create_connection(("127.0.0.1", 9000), timeout=2) as s:
                s.sendall(b"\xff" + frame(1, 999, 0)[1:])
                self.assertEqual(s.recv(32), b"")
            with socket.create_connection(("127.0.0.1", 9000), timeout=2) as s:
                s.sendall(frame(1, 888, 0)[:5])
            time.sleep(0.2)
            m = ipc()
            self.assertEqual(m["processed"], 64 * 21 + 2)
            self.assertEqual(m["missing"], 0)
            self.assertGreaterEqual(m["invalid"], 1)
            self.assertEqual(p.poll(), None)
            # TCP reset is a connection error, distinct from graceful EOF.
            with socket.create_connection(("127.0.0.1",9000),timeout=2) as s:
                s.sendall(frame(1,9998,0)); ack(s)
                s.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack("ii",1,0))
            time.sleep(.2)
            self.assertGreater(ipc()["errors"],0)
            # Duplicate active IDs cannot steal a session.
            with socket.create_connection(("127.0.0.1", 9000), timeout=2) as a:
                a.sendall(frame(1, 9999, 0))
                ack(a)
                with socket.create_connection(("127.0.0.1", 9000), timeout=2) as b:
                    b.sendall(frame(1, 9999, 0))
                    self.assertEqual(b.recv(32), b"")
                a.sendall(frame(3, 9999, 1))
                ack(a)
            # Termination with a live client must unblock all threads.
            live = socket.create_connection(("127.0.0.1", 9000), timeout=2)
            p.terminate()
            self.assertEqual(p.wait(timeout=5), 0)
            live.close()
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()

    def test_01_baseline(self):
        self.exercise("baseline")

    def test_02_epoll(self):
        self.exercise("epoll")

    def test_03_simulator(self):
        p = subprocess.Popen([GATEWAY, "--ipc", "/tmp/pulse-test.sock"])
        try:
            time.sleep(0.2)
            r = subprocess.run(
                [
                    SIMULATOR,
                    "--devices",
                    "100",
                    "--rate",
                    "10",
                    "--duration",
                    "2",
                    "--partial",
                    "3",
                ],
                capture_output=True,
                text=True,
                timeout=10,
                check=True,
            )
            m = json.loads(r.stdout.splitlines()[-1])
            self.assertGreater(m["acknowledged"], 500)
            self.assertEqual(m["sent"], m["acknowledged"])
            self.assertEqual(m["failed"], 0)
            self.assertGreater(ipc()["processed"], m["acknowledged"])
        finally:
            p.terminate()
            self.assertEqual(p.wait(timeout=5), 0)

    def test_04_faults_and_overload(self):
        p = subprocess.Popen(
            [
                GATEWAY,
                "--ipc",
                "/tmp/pulse-test.sock",
                "--queue",
                "1",
                "--workers",
                "1",
                "--ack-delay-ms",
                "10",
            ]
        )
        try:
            time.sleep(0.2)
            r = subprocess.run(
                [
                    SIMULATOR,
                    "--devices",
                    "100",
                    "--rate",
                    "50",
                    "--duration",
                    "2",
                    "--partial",
                    "7",
                    "--malformed",
                    ".1",
                    "--disconnect-probability",
                    ".2",
                    "--slow-readers",
                    "5",
                ],
                capture_output=True,
                text=True,
                timeout=12,
                check=True,
            )
            stats = json.loads(r.stdout.splitlines()[-1])
            self.assertGreater(stats["acknowledged"], 0)
            self.assertEqual(stats["sent"], stats["acknowledged"] + stats["failed"])
            m = ipc()
            self.assertGreater(m["invalid"], 0)
            self.assertGreater(m["overflows"], 0)
            self.assertEqual(p.poll(), None)
            # A healthy new client must work after overload and faults.
            with socket.create_connection(("127.0.0.1", 9000), timeout=3) as s:
                s.sendall(frame(1, 12345, 0))
                ack(s)
        finally:
            p.terminate()
            self.assertEqual(p.wait(timeout=5), 0)

    def test_05_idle_timeout_and_fd_recovery(self):
        for mode in ("baseline", "epoll"):
            p = subprocess.Popen(
                [
                    GATEWAY,
                    "--mode",
                    mode,
                    "--ipc",
                    "/tmp/pulse-test.sock",
                    "--idle-timeout-ms",
                    "250",
                ]
            )
            try:
                time.sleep(0.2)
                before = ipc()["open_fds"]
                with socket.create_connection(("127.0.0.1", 9000), timeout=2) as s:
                    s.sendall(frame(1, 1, 0))
                    ack(s)
                    time.sleep(0.5)
                    self.assertEqual(s.recv(1), b"")
                for _ in range(100):
                    with socket.create_connection(("127.0.0.1", 9000), timeout=2) as s:
                        s.sendall(b"partial")
                time.sleep(0.5)
                m = ipc()
                self.assertEqual(m["active"], 0)
                self.assertLessEqual(m["open_fds"], before)
            finally:
                p.terminate()
                self.assertEqual(p.wait(timeout=5), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
