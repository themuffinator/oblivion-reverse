import json
import subprocess
import sys
from pathlib import Path
import unittest


class SpawnManifestSnapshotTest(unittest.TestCase):
    def test_manifest_matches_snapshot(self) -> None:
        repo_root = Path(__file__).resolve().parents[1]
        script = repo_root / "tools" / "extract_spawn_manifest.py"
        result = subprocess.run(
            [sys.executable, str(script)],
            check=True,
            capture_output=True,
            text=True,
        )
        current = json.loads(result.stdout)
        expected_path = repo_root / "tests" / "expected_spawn_manifest.json"
        expected = json.loads(expected_path.read_text(encoding="utf-8"))
        hlil_manifest = current.get("combined", {}).get("hlil", {})
        self.assertGreater(
            len(hlil_manifest),
            0,
            "Parsed HLIL manifest should contain at least one classname",
        )
        self.assertIn(
            "monster_jorg",
            hlil_manifest,
            "Expected monster_jorg entry missing from HLIL manifest",
        )
        self.assertEqual(
            hlil_manifest["monster_jorg"].get("function"),
            "sub_10001ac0",
            "monster_jorg HLIL manifest entry does not match expected function",
        )
        self.assertIn(
            "weapon_rtdu",
            hlil_manifest,
            "Expected weapon_rtdu entry missing from HLIL manifest",
        )
        self.assertEqual(
            hlil_manifest["weapon_rtdu"].get("function"),
            "SpawnItemFromItemlist",
            "weapon_rtdu HLIL manifest entry does not match expected function",
        )
        self.assertEqual(current, expected)


if __name__ == "__main__":
    unittest.main()
