#!/usr/bin/env python3
"""Dependency-free sidecar used to exercise the C protocol contract."""

import json
import math
import os
import sys


def emit(payload):
    sys.stdout.write(json.dumps(payload, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def singleton(mask):
    return mask > 0 and mask & (mask - 1) == 0


class FakeSession:
    def __init__(self):
        self.spec = None
        self.observations = 0

    def init(self, command):
        required = ("mode", "w", "h", "ntiles", "domains", "cdir", "unary")
        if any(key not in command for key in required):
            raise ValueError("init is missing a required field")
        if command.get("v") != 1:
            raise ValueError("unsupported protocol version")
        w, h, ntiles = command["w"], command["h"], command["ntiles"]
        if any(isinstance(value, bool) or not isinstance(value, int) or value < 1
               for value in (w, h, ntiles)):
            raise ValueError("invalid dimensions")
        if len(command["domains"]) != w * h:
            raise ValueError("wrong domain length")
        if len(command["unary"]) != ntiles:
            raise ValueError("wrong unary length")
        if len(command["cdir"]) != 4 or any(len(row) != ntiles for row in command["cdir"]):
            raise ValueError("wrong compatibility shape")
        self.spec = command
        self.observations = 0
        emit({"v": 1, "t": "ready", "schema": 1})

    def _neighbors(self, index, direction):
        w, h = self.spec["w"], self.spec["h"]
        x, y = index % w, index // w
        nx, ny = x, y
        if direction == 0:
            ny -= 1
        elif direction == 1:
            nx += 1
        elif direction == 2:
            ny += 1
        else:
            nx -= 1
        if self.spec.get("torus", False):
            nx %= w
            ny %= h
        if nx < 0 or ny < 0 or nx >= w or ny >= h:
            return None
        return ny * w + nx

    def _locally_safe(self, domains, index, tile):
        for direction in range(4):
            neighbor = self._neighbors(index, direction)
            if neighbor is None:
                continue
            compatible = self.spec["cdir"][direction][tile]
            if int(domains[neighbor]) & int(compatible) == 0:
                return False
        return True

    def _done(self, domains):
        if os.environ.get("FAKE_INVALID_DONE"):
            cfg = [index % 2 for index in range(self.spec["w"] * self.spec["h"])]
            emit({"v": 1, "t": "done", "valid": 1, "quality": 1.0, "cfg": cfg})
            return True
        if not all(singleton(int(mask)) for mask in domains):
            return False
        cfg = [(int(mask) & -int(mask)).bit_length() - 1 for mask in domains]
        emit({"v": 1, "t": "done", "valid": 1, "quality": 1.0, "cfg": cfg})
        return True

    def sample(self, command):
        domains = command.get("domains")
        if not isinstance(domains, list) or len(domains) != self.spec["w"] * self.spec["h"]:
            raise ValueError("sample has the wrong domain length")
        if self._done(domains):
            return
        for index, raw_mask in enumerate(domains):
            mask = int(raw_mask)
            if singleton(mask):
                continue
            for tile in range(self.spec["ntiles"]):
                if mask & (1 << tile) and self._locally_safe(domains, index, tile):
                    emit({"v": 1, "t": "stats", "beta": 1.0,
                          "energy": 0.0, "bad": 0, "confidence": 1.0,
                          "reward": 0.0, "observations": self.observations})
                    emit({"v": 1, "t": "proposal",
                          "patch": [{"i": index, "tile": tile, "p": 1.0}],
                          "beta": 1.0, "energy": 0.0, "bad": 0})
                    return
        raise ValueError("no locally safe proposal")

    def feedback(self, command):
        reward = float(command.get("reward", 0.0))
        if not math.isfinite(reward):
            raise ValueError("feedback reward is not finite")
        # FAKE_FEEDBACK_LOG lets a test inspect the frames C actually sends
        log = os.environ.get("FAKE_FEEDBACK_LOG")
        if log:
            with open(log, "a", encoding="utf-8") as handle:
                handle.write(json.dumps(command, separators=(",", ":")) + "\n")
        self.observations += 1
        emit({"v": 1, "t": "learn", "tile_bias": [], "pair_bias": [],
              "context_bias": [], "observations": self.observations})

    def reset(self):
        self.observations = 0
        emit({"v": 1, "t": "learn", "tile_bias": [], "pair_bias": [],
              "context_bias": [], "observations": 0})


def main():
    session = FakeSession()
    try:
        for line in sys.stdin:
            try:
                command = json.loads(line)
                if not isinstance(command, dict):
                    raise ValueError("command must be an object")
                kind = command.get("t")
                if kind == "init":
                    session.init(command)
                elif kind == "sample":
                    session.sample(command)
                elif kind == "feedback":
                    session.feedback(command)
                elif kind == "reset":
                    session.reset()
                elif kind == "finish":
                    session._done(command.get("assignments", []))
                elif kind == "stop":
                    return 0
                else:
                    raise ValueError("unknown command")
            except Exception as error:  # noqa: BLE001 - test protocol boundary
                emit({"v": 1, "t": "fatal", "why": str(error)})
                return 1
    except BrokenPipeError:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
