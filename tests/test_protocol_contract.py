import json
import os
import select
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "tests" / "fake_thermo.py"
REAL_WORKER = ROOT / "wfc_thermo.py"


def read_event(proc):
    buffer = getattr(proc, "_event_buffer", b"")
    deadline = time.monotonic() + 2.0
    while b"\n" not in buffer:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError("timed out waiting for sidecar event")
        ready, _, _ = select.select([proc.stdout], [], [], remaining)
        if not ready:
            raise AssertionError("timed out waiting for sidecar event")
        chunk = os.read(proc.stdout.fileno(), 4096)
        if not chunk:
            raise AssertionError("sidecar closed its output")
        buffer += chunk
    line, _, buffer = buffer.partition(b"\n")
    proc._event_buffer = buffer
    return json.loads(line.decode("utf-8"))


def send(proc, payload):
    proc.stdin.write((json.dumps(payload) + "\n").encode("utf-8"))
    proc.stdin.flush()


def init_payload(domains=None):
    return {
        "v": 1,
        "t": "init",
        "mode": "streets",
        "w": 3,
        "h": 2,
        "ntiles": 2,
        "seed": 1,
        "torus": False,
        "smooth": False,
        "unary": [1.0, 1.0],
        "cdir": [[3, 3] for _ in range(4)],
        "domains": domains or [3] * 6,
        "learn": True,
    }


class ProtocolContractTests(unittest.TestCase):
    def setUp(self):
        self.proc = None

    def tearDown(self):
        if self.proc is not None:
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait(timeout=2)
            for stream in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
                if stream is not None:
                    stream.close()

    def launch(self):
        self.assertTrue(WORKER.exists(), "fake thermo worker has not been added")
        return self.launch_worker(WORKER)

    def launch_worker(self, worker):
        self.assertTrue(worker.exists(), f"thermo worker has not been added: {worker}")
        self.proc = subprocess.Popen(
            [sys.executable, os.fspath(worker)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        return self.proc

    def test_real_worker_handshake_and_stop(self):
        proc = self.launch_worker(REAL_WORKER)
        send(proc, init_payload())
        ready = read_event(proc)
        self.assertEqual(ready["t"], "ready")
        self.assertEqual(ready["sampler"], "python")
        send(proc, {"v": 1, "t": "stop"})
        self.assertEqual(proc.wait(timeout=2), 0)

    def test_real_worker_learns_incrementally_and_persists_profile(self):
        with tempfile.TemporaryDirectory() as profile_dir:
            proc = self.launch_worker(REAL_WORKER)
            domains = [3] * 6
            payload = init_payload(domains)
            payload["profile_dir"] = profile_dir
            send(proc, payload)
            ready = read_event(proc)
            self.assertEqual(ready["t"], "ready")
            for _ in range(len(domains)):
                send(proc, {"v": 1, "t": "sample", "domains": domains,
                            "budget": 4, "beta_target": 2.0})
                self.assertEqual(read_event(proc)["t"], "stats")
                proposal = read_event(proc)
                self.assertEqual(proposal["t"], "proposal")
                item = proposal["patch"][0]
                domains[item["i"]] = 1 << item["tile"]
                send(proc, {"v": 1, "t": "feedback", "reward": 0.5,
                            "quality": 0.8, "accepted": 1, "rejected": 0,
                            "contradictions": 0, "tile_events": [
                                {"index": item["tile"], "value": 1.0}],
                            "pair_events": [], "context_events": [],
                            "final": False})
                learned = read_event(proc)
                self.assertEqual(learned["t"], "learn")
                self.assertEqual(learned["observations"], _ + 1)
            send(proc, {"v": 1, "t": "sample", "domains": domains,
                        "budget": 4, "beta_target": 2.0})
            done = read_event(proc)
            self.assertEqual(done["t"], "done")
            self.assertEqual(done["valid"], 1)
            send(proc, {"v": 1, "t": "stop"})
            self.assertEqual(proc.wait(timeout=2), 0)
            self.assertEqual(len(list(Path(profile_dir).glob("*.json"))), 1)

    def test_handshake_and_stop(self):
        proc = self.launch()
        send(proc, init_payload())
        self.assertEqual(read_event(proc)["t"], "ready")
        send(proc, {"v": 1, "t": "stop"})
        self.assertEqual(proc.wait(timeout=2), 0)

    def test_sample_feedback_and_incremental_completion(self):
        proc = self.launch()
        domains = [3] * 6
        send(proc, init_payload(domains))
        self.assertEqual(read_event(proc)["t"], "ready")
        for _ in range(len(domains)):
            send(proc, {"v": 1, "t": "sample", "domains": domains,
                        "budget": 4, "beta_target": 2.0})
            self.assertEqual(read_event(proc)["t"], "stats")
            proposal = read_event(proc)
            self.assertEqual(proposal["t"], "proposal")
            self.assertEqual(len(proposal["patch"]), 1)
            item = proposal["patch"][0]
            domains[item["i"]] = 1 << item["tile"]
            send(proc, {"v": 1, "t": "feedback", "reward": 0.25,
                        "quality": 0.5, "accepted": 1, "rejected": 0,
                        "contradictions": 0, "tile_events": [],
                        "pair_events": [], "context_events": [],
                        "final": False})
            self.assertEqual(read_event(proc)["t"], "learn")
        send(proc, {"v": 1, "t": "sample", "domains": domains,
                    "budget": 4, "beta_target": 2.0})
        self.assertEqual(read_event(proc)["t"], "done")

    def test_feedback_event_batches_are_bounded(self):
        proc = self.launch_worker(REAL_WORKER)
        send(proc, init_payload())
        self.assertEqual(read_event(proc)["t"], "ready")
        send(proc, {
            "v": 1, "t": "feedback", "reward": 0.0,
            "tile_events": [{}] * 513,
            "pair_events": [], "context_events": [],
        })
        self.assertEqual(read_event(proc)["t"], "fatal")
        self.assertEqual(proc.wait(timeout=2), 1)

    def test_protocol_frames_are_bounded(self):
        proc = self.launch_worker(REAL_WORKER)
        proc.stdin.write(("{" + "x" * (8 * 1024 * 1024) + "}\n").encode("ascii"))
        proc.stdin.flush()
        self.assertEqual(read_event(proc)["t"], "fatal")
        self.assertEqual(proc.wait(timeout=2), 1)

    def test_unknown_command_emits_fatal_and_exits(self):
        proc = self.launch()
        send(proc, init_payload())
        self.assertEqual(read_event(proc)["t"], "ready")
        send(proc, {"v": 1, "t": "unknown"})
        self.assertEqual(read_event(proc)["t"], "fatal")
        self.assertEqual(proc.wait(timeout=2), 1)


if __name__ == "__main__":
    unittest.main()
