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
        repo_manifest = current.get("combined", {}).get("repo", {})
        self.assertIn(
            "monster_makron",
            repo_manifest,
            "Expected monster_makron entry missing from repo manifest",
        )
        self.assertEqual(
            repo_manifest["monster_makron"].get("function"),
            "SP_monster_makron",
            "monster_makron repo manifest entry does not match expected function",
        )
        missing_in_repo = current.get("comparison", {}).get("missing_in_repo", [])
        self.assertNotIn(
            "monster_makron",
            missing_in_repo,
            "monster_makron should be spawnable by maps but is still reported missing",
        )
        self.assertEqual(current, expected)


class SpawnManifestControllersTest(unittest.TestCase):
    def test_target_controllers_are_extracted(self) -> None:
        repo_root = Path(__file__).resolve().parents[1]
        script = repo_root / "tools" / "extract_spawn_manifest.py"
        result = subprocess.run(
            [sys.executable, str(script)],
            check=True,
            capture_output=True,
            text=True,
        )
        manifest = json.loads(result.stdout)
        hlil_manifest = manifest.get("combined", {}).get("hlil", {})
        required = {
            "target_actor": "target_actor controllers should be present",
            "target_crosslevel_target": "Missing target_crosslevel_target controller entry",
        }
        for classname, message in required.items():
            self.assertIn(classname, hlil_manifest, message)
            func_name = hlil_manifest[classname].get("function", "")
            self.assertTrue(
                func_name.startswith("sub_"),
                f"{classname} should dispatch to a sub_* function, got {func_name!r}",
            )


if __name__ == "__main__":
    unittest.main()
