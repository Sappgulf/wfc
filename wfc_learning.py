"""Small, dependency-free online learner used by the thermo sidecar.

The learner changes soft preferences only.  Compatibility and domain masks stay
under the C generator's control.
"""

import hashlib
import json
import math
import os
import tempfile


PROFILE_VERSION = 1
BIAS_LIMIT = 2.5
HISTORY_LIMIT = 64
CONTEXT_COUNT = 8
METRIC_KEYS = (
    "total",
    "validity",
    "boundary",
    "coverage",
    "diversity",
    "smoothness",
    "stability",
    "topology",
)


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def _clamp(value, low, high):
    return max(low, min(high, value))


def _require_dimension(value, name):
    if not _is_int(value) or value < 0:
        raise ValueError("%s must be a non-negative integer" % name)
    return value


def _finite(value, name):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError("%s must be finite" % name)
    return number


def tile_fingerprint(mode, tiles, cdir):
    """Return a stable 16-hex schema fingerprint for one mode."""
    payload = json.dumps(
        {"mode": str(mode), "tiles": tiles, "cdir": cdir},
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()[:16]


def new_state(ntiles, pair_count, context_count=CONTEXT_COUNT):
    """Return a zeroed learner state with the requested dimensions."""
    ntiles = _require_dimension(ntiles, "ntiles")
    pair_count = _require_dimension(pair_count, "pair_count")
    context_count = _require_dimension(context_count, "context_count")
    return {
        "version": PROFILE_VERSION,
        "mode": "",
        "fingerprint": "",
        "observations": 0,
        "baseline": 0.0,
        "tile_bias": [0.0] * ntiles,
        "pair_bias": [0.0] * pair_count,
        "context_bias": [0.0] * context_count,
        "quality_history": [],
        "metrics_history": [],
    }


def _event_index(item, values, label):
    if not isinstance(item, dict):
        raise ValueError("%s event must be an object" % label)
    raw = item.get("index", item.get("tile"))
    if not _is_int(raw) or raw < 0 or raw >= len(values):
        raise ValueError("%s event index is out of range" % label)
    return raw


def _apply_events(values, events, delta, learning_rate, decay, label):
    for item in events or []:
        index = _event_index(item, values, label)
        feature = _clamp(_finite(item.get("value", 1.0), "%s event value" % label), -1.0, 1.0)
        values[index] = _clamp(
            values[index] + learning_rate * delta * feature,
            -BIAS_LIMIT,
            BIAS_LIMIT,
        )


def _normalize_metrics(metrics):
    if metrics is None:
        return None
    if not isinstance(metrics, dict):
        raise ValueError("metrics must be an object")
    normalized = {}
    for key in METRIC_KEYS:
        if key not in metrics:
            continue
        value = _finite(metrics[key], "%s metric" % key)
        if not 0.0 <= value <= 1.0:
            raise ValueError("%s metric is out of range" % key)
        normalized[key] = value
    if "focus" in metrics:
        focus = metrics["focus"]
        if not isinstance(focus, str) or len(focus) > 32:
            raise ValueError("metric focus must be a short string")
        normalized["focus"] = focus
    return normalized


def _validate_state(state, ntiles, pair_count, context_count=CONTEXT_COUNT):
    if not isinstance(state, dict):
        raise ValueError("profile state must be an object")
    ntiles = _require_dimension(ntiles, "ntiles")
    pair_count = _require_dimension(pair_count, "pair_count")
    context_count = _require_dimension(context_count, "context_count")
    if state.get("version") != PROFILE_VERSION:
        raise ValueError("unsupported profile version")
    if not _is_int(state.get("observations")) or state["observations"] < 0:
        raise ValueError("observations must be a non-negative integer")
    baseline = _finite(state.get("baseline"), "baseline")
    if not -1.0 <= baseline <= 1.0:
        raise ValueError("baseline is out of range")
    for key, size in (("tile_bias", ntiles), ("pair_bias", pair_count),
                      ("context_bias", context_count)):
        values = state.get(key)
        if not isinstance(values, list) or len(values) != size:
            raise ValueError("%s has the wrong length" % key)
        for value in values:
            number = _finite(value, key)
            if not -BIAS_LIMIT <= number <= BIAS_LIMIT:
                raise ValueError("%s contains an out-of-range bias" % key)
    history = state.get("quality_history")
    if not isinstance(history, list) or len(history) > HISTORY_LIMIT:
        raise ValueError("quality_history is invalid")
    for value in history:
        _finite(value, "quality_history value")
    metrics_history = state.get("metrics_history", [])
    if not isinstance(metrics_history, list) or len(metrics_history) > HISTORY_LIMIT:
        raise ValueError("metrics_history is invalid")
    for metrics in metrics_history:
        _normalize_metrics(metrics)
    return state


def update_state(state, reward, tile_events, pair_events, context_events,
                 learning_rate=0.06, decay=0.995, metrics=None):
    """Apply one bounded reward update and return the mutated state."""
    if not isinstance(state, dict):
        raise ValueError("state must be an object")
    reward = _finite(reward, "reward")
    learning_rate = _finite(learning_rate, "learning_rate")
    decay = _finite(decay, "decay")
    normalized_metrics = _normalize_metrics(metrics)
    if learning_rate <= 0.0 or learning_rate > 1.0:
        raise ValueError("learning_rate must be in (0, 1]")
    if decay < 0.0 or decay > 1.0:
        raise ValueError("decay must be in [0, 1]")
    _validate_state(
        state,
        len(state.get("tile_bias", [])),
        len(state.get("pair_bias", [])),
        len(state.get("context_bias", [])),
    )
    reward = _clamp(reward, -1.0, 1.0)
    delta = _clamp(reward - state["baseline"], -1.0, 1.0)
    state["baseline"] = _clamp(
        state["baseline"] * 0.95 + reward * 0.05,
        -1.0,
        1.0,
    )
    for values in (state["tile_bias"], state["pair_bias"], state["context_bias"]):
        for index, value in enumerate(values):
            values[index] = _clamp(value * decay, -BIAS_LIMIT, BIAS_LIMIT)
    _apply_events(state["tile_bias"], tile_events, delta, learning_rate, decay, "tile")
    _apply_events(state["pair_bias"], pair_events, delta, learning_rate, decay, "pair")
    _apply_events(state["context_bias"], context_events, delta, learning_rate, decay, "context")
    state["observations"] += 1
    state["quality_history"].append(reward)
    del state["quality_history"][:-HISTORY_LIMIT]
    state.setdefault("metrics_history", [])
    if normalized_metrics is not None:
        state["metrics_history"].append(normalized_metrics)
        del state["metrics_history"][:-HISTORY_LIMIT]
    return state


def reset_state(state):
    """Return a fresh zeroed state preserving dimensions and identity."""
    _validate_state(
        state,
        len(state.get("tile_bias", [])),
        len(state.get("pair_bias", [])),
        len(state.get("context_bias", [])),
    )
    fresh = new_state(
        len(state["tile_bias"]),
        len(state["pair_bias"]),
        len(state["context_bias"]),
    )
    fresh["mode"] = state.get("mode", "")
    fresh["fingerprint"] = state.get("fingerprint", "")
    return fresh


def _fresh_identity(mode, fingerprint, ntiles, pair_count, context_count):
    state = new_state(ntiles, pair_count, context_count)
    state["mode"] = str(mode)
    state["fingerprint"] = str(fingerprint)
    return state


def load_profile(path, mode, fingerprint, ntiles, pair_count,
                 context_count=CONTEXT_COUNT):
    """Load a matching profile or return a fresh state on invalid input."""
    fresh = _fresh_identity(mode, fingerprint, ntiles, pair_count, context_count)
    try:
        with open(path, "r", encoding="utf-8") as profile_file:
            payload = json.load(profile_file)
        if not isinstance(payload, dict):
            return fresh
        if payload.get("version") != PROFILE_VERSION:
            return fresh
        if payload.get("mode") != str(mode) or payload.get("fingerprint") != str(fingerprint):
            return fresh
        state = dict(payload)
        _validate_state(state, ntiles, pair_count, context_count)
        state["mode"] = str(mode)
        state["fingerprint"] = str(fingerprint)
        state.setdefault("metrics_history", [])
        return state
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
        return fresh


def _profile_payload(mode, fingerprint, state):
    payload = dict(state)
    payload["version"] = PROFILE_VERSION
    payload["mode"] = str(mode)
    payload["fingerprint"] = str(fingerprint)
    _validate_state(
        payload,
        len(payload.get("tile_bias", [])),
        len(payload.get("pair_bias", [])),
        len(payload.get("context_bias", [])),
    )
    return payload


def save_profile(path, mode, fingerprint, state):
    """Atomically write a validated profile with private file permissions."""
    payload = _profile_payload(mode, fingerprint, state)
    directory = os.path.dirname(os.path.abspath(path)) or "."
    os.makedirs(directory, mode=0o700, exist_ok=True)
    try:
        os.chmod(directory, 0o700)
    except OSError:
        pass
    temp_fd = -1
    temp_path = None
    try:
        temp_fd, temp_path = tempfile.mkstemp(
            prefix=".%s." % os.path.basename(path),
            suffix=".tmp",
            dir=directory,
        )
        os.chmod(temp_path, 0o600)
        with os.fdopen(temp_fd, "w", encoding="utf-8") as profile_file:
            temp_fd = -1
            json.dump(payload, profile_file, sort_keys=True, separators=(",", ":"))
            profile_file.write("\n")
            profile_file.flush()
            try:
                os.fsync(profile_file.fileno())
            except OSError:
                pass
        os.replace(temp_path, path)
        temp_path = None
    finally:
        if temp_fd >= 0:
            os.close(temp_fd)
        if temp_path:
            try:
                os.unlink(temp_path)
            except OSError:
                pass
