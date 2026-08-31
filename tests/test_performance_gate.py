import unittest


from tests.performance_gate import compare


def report(*, p95_ms=10.0, median_quality=0.95, successes=4, cases=4,
           trials=2):
    return {
        "schema": 2,
        "trials": trials,
        "dimensions": {"w": 8, "h": 6},
        "summary": {
            "classic": {
                "cases": cases,
                "successes": successes,
                "median_ms": 5.0,
                "p95_ms": p95_ms,
                "median_quality": median_quality,
            },
        },
    }


class PerformanceGateTests(unittest.TestCase):
    def test_accepts_report_inside_slo_budget(self):
        budget = {
            "benchmark": {"trials": 2, "w": 8, "h": 6},
            "solvers": {
                "classic": {
                    "min_successes": 4,
                    "max_p95_ms": 20.0,
                    "min_median_quality": 0.90,
                },
            },
        }
        self.assertEqual(compare(report(), budget), [])

    def test_reports_slo_violations_with_measured_values(self):
        budget = {
            "benchmark": {"trials": 2, "w": 8, "h": 6},
            "solvers": {
                "classic": {
                    "min_successes": 4,
                    "max_p95_ms": 20.0,
                    "min_median_quality": 0.90,
                },
            },
        }
        violations = compare(report(p95_ms=21.0, median_quality=0.89), budget)
        self.assertEqual({item["metric"] for item in violations}, {
            "p95_ms", "median_quality",
        })
        self.assertTrue(all(item["solver"] == "classic" for item in violations))

    def test_reports_latency_and_quality_trend_regressions(self):
        budget = {
            "benchmark": {"trials": 2, "w": 8, "h": 6},
            "trend": {"max_p95_ratio": 1.5, "max_quality_drop": 0.02},
            "solvers": {
                "classic": {
                    "min_successes": 4,
                    "max_p95_ms": 200.0,
                    "min_median_quality": 0.80,
                },
            },
        }
        baseline = report(p95_ms=100.0, median_quality=0.95)
        violations = compare(
            report(p95_ms=160.0, median_quality=0.92), budget, baseline,
        )
        self.assertEqual({item["metric"] for item in violations}, {
            "p95_ms", "median_quality",
        })
        self.assertTrue(all(item["rule"] == "trend" for item in violations))

    def test_reports_malformed_metrics_without_crashing(self):
        budget = {
            "benchmark": {"trials": 2, "w": 8, "h": 6},
            "solvers": {
                "classic": {
                    "min_successes": 4,
                    "max_p95_ms": 20.0,
                    "min_median_quality": 0.90,
                },
            },
        }
        malformed = report()
        malformed["summary"]["classic"]["p95_ms"] = None
        malformed["summary"]["classic"]["median_quality"] = "not-a-number"
        violations = compare(malformed, budget)
        self.assertEqual({item["metric"] for item in violations}, {
            "p95_ms", "median_quality",
        })
        self.assertTrue(all(item["rule"] == "invalid" for item in violations))


if __name__ == "__main__":
    unittest.main()
