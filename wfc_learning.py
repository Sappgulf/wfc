"""Small, dependency-free online learner used by the thermo sidecar.

The learner changes soft preferences only.  Compatibility and domain masks stay
under the C generator's control.
"""

import hashlib
import json
import math
import os
import tempfile


PROFILE_VERSION = 2
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
QUALITY_COMPONENT_KEYS = METRIC_KEYS[1:]


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
        "objective_history": [],
    }


def _event_index(item, values, label):
    if not isinstance(item, dict):
        raise ValueError("%s event must be an object" % label)
    raw = item.get("index", item.get("tile"))
    if not _is_int(raw) or raw < 0 or raw >= len(values):
        raise ValueError("%s event index is out of range" % label)
    return raw


def _apply_events(values, events, delta, learning_rate, decay, label):
    """Decay is applied per visit, not per update.

    A round touches one cell, so it reports a handful of features out of
    hundreds. Decaying every bias on every update meant an untouched feature
    lost ~80% of its value per run (0.995 ** ~325 steps) while a touched one
    gained learning_rate * delta — nothing could ever accumulate, and the
    learner sat at a flat displacement rate no matter how long it ran.
    """
    for item in events or []:
        index = _event_index(item, values, label)
        feature = _clamp(_finite(item.get("value", 1.0), "%s event value" % label), -1.0, 1.0)
        values[index] = _clamp(
            values[index] * decay + learning_rate * delta * feature,
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


def _normalize_metric_delta(delta):
    """Validate a signed quality-vector delta in the range [-1, 1]."""
    if delta is None:
        return None
    if not isinstance(delta, dict):
        raise ValueError("metrics_delta must be an object")
    normalized = {}
    for key in METRIC_KEYS:
        if key not in delta:
            continue
        value = _finite(delta[key], "%s metric delta" % key)
        if not -1.0 <= value <= 1.0:
            raise ValueError("%s metric delta is out of range" % key)
        normalized[key] = value
    if "focus" in delta:
        focus = delta["focus"]
        if not isinstance(focus, str) or len(focus) > 32:
            raise ValueError("metric delta focus must be a short string")
        normalized["focus"] = focus
    return normalized


def _normalize_objective_weights(weights):
    """Return a positive, bounded mode objective with seven components."""
    if weights is None:
        return {key: 1.0 / len(QUALITY_COMPONENT_KEYS)
                for key in QUALITY_COMPONENT_KEYS}
    if not isinstance(weights, dict):
        raise ValueError("quality_weights must be an object")
    normalized = {}
    total = 0.0
    for key in QUALITY_COMPONENT_KEYS:
        if key not in weights:
            raise ValueError("quality_weights missing %s" % key)
        value = _finite(weights[key], "%s quality weight" % key)
        if value < 0.0 or value > 1.0:
            raise ValueError("%s quality weight is out of range" % key)
        normalized[key] = value
        total += value
    if total <= 0.0 or total > 1.000001:
        raise ValueError("quality_weights must sum to (0, 1]")
    return normalized


def objective_signal(reward, metric_delta=None, objective_weights=None):
    """Blend acceptance reward with the active mode's weighted improvement."""
    reward = _clamp(_finite(reward, "reward"), -1.0, 1.0)
    delta = _normalize_metric_delta(metric_delta)
    if not delta:
        return reward
    weights = _normalize_objective_weights(objective_weights)
    weighted = sum(weights[key] * delta.get(key, 0.0)
                   for key in QUALITY_COMPONENT_KEYS)
    total_delta = delta.get("total", 0.0)
    improvement = 0.35 * total_delta + 0.65 * weighted
    return _clamp(reward + 0.35 * improvement, -1.0, 1.0)


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
    objective_history = state.get("objective_history", [])
    if not isinstance(objective_history, list) or len(objective_history) > HISTORY_LIMIT:
        raise ValueError("objective_history is invalid")
    for item in objective_history:
        if not isinstance(item, dict):
            raise ValueError("objective history item must be an object")
        for key in ("reward", "signal"):
            value = _finite(item.get(key), "objective history %s" % key)
            if not -1.0 <= value <= 1.0:
                raise ValueError("objective history %s is out of range" % key)
        _normalize_metric_delta(item.get("delta", {}))
        focus = item.get("focus", "")
        if not isinstance(focus, str) or len(focus) > 32:
            raise ValueError("objective history focus must be a short string")
    return state


def update_state(state, reward, tile_events, pair_events, context_events,
                 learning_rate=0.06, decay=0.995, metrics=None,
                 metric_delta=None, objective_weights=None):
    """Apply one bounded reward update and return the mutated state."""
    if not isinstance(state, dict):
        raise ValueError("state must be an object")
    reward = _finite(reward, "reward")
    learning_rate = _finite(learning_rate, "learning_rate")
    decay = _finite(decay, "decay")
    normalized_metrics = _normalize_metrics(metrics)
    normalized_delta = _normalize_metric_delta(metric_delta)
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
    signal = objective_signal(reward, normalized_delta, objective_weights)
    delta = _clamp(signal - state["baseline"], -1.0, 1.0)
    # A fast baseline swallows the signal: at 0.05 it tracked the reward
    # within ~20 rounds, so `delta` sat at zero for the rest of the run.
    state["baseline"] = _clamp(
        state["baseline"] * 0.99 + signal * 0.01,
        -1.0,
        1.0,
    )
    _apply_events(state["tile_bias"], tile_events, delta, learning_rate, decay, "tile")
    _apply_events(state["pair_bias"], pair_events, delta, learning_rate, decay, "pair")
    _apply_events(state["context_bias"], context_events, delta, learning_rate, decay, "context")
    state["observations"] += 1
    state["quality_history"].append(signal)
    del state["quality_history"][:-HISTORY_LIMIT]
    state.setdefault("metrics_history", [])
    if normalized_metrics is not None:
        state["metrics_history"].append(normalized_metrics)
        del state["metrics_history"][:-HISTORY_LIMIT]
    state.setdefault("objective_history", [])
    state["objective_history"].append({
        "reward": reward,
        "signal": signal,
        "delta": normalized_delta or {},
        "focus": (normalized_metrics or {}).get("focus", ""),
    })
    del state["objective_history"][:-HISTORY_LIMIT]
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
        state.setdefault("objective_history", [])
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
