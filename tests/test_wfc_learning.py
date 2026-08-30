import json
import tempfile
import unittest
from pathlib import Path

from wfc_learning import (
    PROFILE_VERSION,
    load_profile,
    new_state,
    reset_state,
    save_profile,
    update_state,
)


class LearnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self.tmp_dir.name)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_positive_reward_increases_observed_features(self):
        state = new_state(4, 16)
        update_state(
            state,
            1.0,
            [{"index": 2, "value": 1.0}],
            [{"index": 5, "value": 1.0}],
            [{"index": 3, "value": 1.0}],
        )
        self.assertGreater(state["tile_bias"][2], 0.0)
        self.assertGreater(state["pair_bias"][5], 0.0)
        self.assertGreater(state["context_bias"][3], 0.0)

    def test_negative_reward_decreases_features_and_updates_baseline(self):
        state = new_state(2, 8)
        update_state(state, -1.0, [{"index": 1, "value": 1.0}], [], [])
        self.assertLess(state["tile_bias"][1], 0.0)
        self.assertLess(state["baseline"], 0.0)
        self.assertEqual(state["observations"], 1)

    def test_biases_are_clamped_and_decay_is_applied(self):
        state = new_state(1, 1)
        for _ in range(200):
            update_state(state, 1.0, [{"index": 0, "value": 100.0}], [], [])
        self.assertLessEqual(state["tile_bias"][0], 2.5)
        state["tile_bias"][0] = 2.5
        update_state(state, 0.0, [], [], [], decay=0.5)
        self.assertLess(state["tile_bias"][0], 2.5)

    def test_profile_fingerprint_mismatch_returns_fresh_state(self):
        path = self.tmp_path / "profile.json"
        save_profile(str(path), "streets", "aaaa", new_state(4, 64))
        loaded = load_profile(str(path), "streets", "bbbb", 4, 64)
        self.assertEqual(loaded["observations"], 0)
        self.assertEqual(loaded["tile_bias"], [0.0] * 4)

    def test_corrupt_profile_is_ignored(self):
        path = self.tmp_path / "profile.json"
        path.write_text("{not json", encoding="utf-8")
        loaded = load_profile(str(path), "neurons", "abcd", 16, 1024)
        self.assertEqual(loaded["observations"], 0)

    def test_save_is_atomic_and_validated(self):
        path = self.tmp_path / "profile.json"
        state = new_state(3, 12)
        save_profile(str(path), "mycelium", "cafe", state)
        payload = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(payload["version"], PROFILE_VERSION)
        self.assertEqual(payload["fingerprint"], "cafe")
        self.assertFalse((self.tmp_path / "profile.json.tmp").exists())

    def test_reset_state_preserves_dimensions_and_clears_observations(self):
        state = new_state(3, 12, 5)
        state["observations"] = 19
        state["tile_bias"][0] = 1.2
        reset = reset_state(state)
        self.assertEqual(reset["observations"], 0)
        self.assertEqual(reset["tile_bias"], [0.0] * 3)
        self.assertEqual(len(reset["pair_bias"]), 12)
        self.assertEqual(len(reset["context_bias"]), 5)

    def test_metric_history_is_bounded_and_persisted(self):
        path = self.tmp_path / "metrics.json"
        state = new_state(2, 4, 3)
        for _ in range(70):
            update_state(
                state,
                0.2,
                [], [], [],
                metrics={"total": 0.7, "topology": 0.8, "focus": "delta"},
            )
        self.assertEqual(len(state["metrics_history"]), 64)
        save_profile(str(path), "delta", "beef", state)
        loaded = load_profile(str(path), "delta", "beef", 2, 4, 3)
        self.assertEqual(len(loaded["metrics_history"]), 64)
        self.assertEqual(loaded["metrics_history"][-1]["focus"], "delta")

    def test_metric_history_rejects_non_finite_values(self):
        state = new_state(1, 1)
        with self.assertRaises(ValueError):
            update_state(
                state,
                0.0,
                [], [], [],
                metrics={"total": float("nan")},
            )

    def test_objective_history_uses_weighted_metric_delta_and_is_bounded(self):
        state = new_state(2, 4)
        weights = {
            "validity": 0.10, "boundary": 0.10, "coverage": 0.10,
            "diversity": 0.10, "smoothness": 0.10, "stability": 0.10,
            "topology": 0.40,
        }
        for _ in range(70):
            update_state(
                state, 0.0, [], [], [],
                metrics={"total": 0.5, "topology": 0.9, "focus": "neurons"},
                metric_delta={"total": 0.0, "topology": 1.0, "focus": "neurons"},
                objective_weights=weights,
            )
        self.assertEqual(len(state["objective_history"]), 64)
        self.assertGreater(state["objective_history"][-1]["signal"], 0.0)
        save_profile(str(self.tmp_path / "objective.json"), "neurons", "beef", state)
        loaded = load_profile(str(self.tmp_path / "objective.json"), "neurons", "beef", 2, 4)
        self.assertEqual(len(loaded["objective_history"]), 64)


if __name__ == "__main__":
    unittest.main()
