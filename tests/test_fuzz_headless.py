import unittest


from tests.fuzz_headless import generate_cases


class FuzzCaseTests(unittest.TestCase):
    def test_case_generation_is_reproducible_and_seeded(self):
        modes = ["circuit", "rail", "waves"]
        first = generate_cases(modes, 16, 20260830)
        repeat = generate_cases(modes, 16, 20260830)
        other = generate_cases(modes, 16, 20260831)
        self.assertEqual(first, repeat)
        self.assertNotEqual(first, other)
        self.assertEqual(len(first), 16)
        for case in first:
            self.assertIn(case["mode"], modes)
            self.assertGreaterEqual(case["w"], 4)
            self.assertGreaterEqual(case["h"], 4)
            self.assertIsInstance(case["seed"], int)


if __name__ == "__main__":
    unittest.main()
