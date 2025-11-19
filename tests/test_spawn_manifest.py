import json
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest


class SpawnManifestSnapshotTest(unittest.TestCase):
    def _extract_manifest(self, defines: list[str] | None = None) -> tuple[dict, Path]:
        repo_root = Path(__file__).resolve().parents[1]
        script = repo_root / "tools" / "extract_spawn_manifest.py"
        cmd = [sys.executable, str(script)]
        for definition in defines or []:
            cmd.extend(["--define", definition])
        result = subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
        )
        return json.loads(result.stdout), repo_root

    def test_manifest_matches_snapshot(self) -> None:
        current, repo_root = self._extract_manifest()
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
            "sub_10014220",
            "weapon_rtdu HLIL manifest entry does not match expected function",
        )
        self.assertEqual(
            hlil_manifest["ammo_rifleplasma"].get("function"),
            "sub_10037c80",
            "ammo_rifleplasma should map to the HLIL function recovered from sub_1000b150",
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

    def test_parity_manifest_omits_oblivion_only_sentinel(self) -> None:
        current, repo_root = self._extract_manifest(["OBLIVION_ENABLE_MONSTER_SENTINEL=0"])
        repo_manifest = current.get("combined", {}).get("repo", {})
        self.assertNotIn(
            "monster_sentinel",
            repo_manifest,
            "monster_sentinel should not be present when the custom flag is disabled",
        )
        comparison = current.get("comparison", {})
        for key in ("missing_in_hlil", "only_in_repo"):
            self.assertNotIn(
                "monster_sentinel",
                comparison.get(key, []),
                f"monster_sentinel should be dropped from {key} when disabled",
            )

        expected_path = repo_root / "tests" / "expected_spawn_manifest.json"
        parity_expected = json.loads(expected_path.read_text(encoding="utf-8"))
        parity_expected.get("combined", {}).get("repo", {}).pop("monster_sentinel", None)
        parity_missing = parity_expected.get("comparison", {}).get("missing_in_hlil", [])
        parity_expected["comparison"]["missing_in_hlil"] = [
            classname for classname in parity_missing if classname != "monster_sentinel"
        ]
        self.assertEqual(current, parity_expected)


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


class SpawnManifestDocsCoverageTest(unittest.TestCase):
    def test_docs_manifest_includes_controller_entries(self) -> None:
        repo_root = Path(__file__).resolve().parents[1]
        manifest_path = repo_root / "docs" / "manifests" / "spawn_manifest.json"
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        hlil_manifest = data.get("hlil", {})
        required = {
            "target_actor": "target_actor entry missing from docs manifest",
            "target_changelevel": "target_changelevel entry missing from docs manifest",
            "target_crosslevel_target": "target_crosslevel_target entry missing from docs manifest",
            "trigger_once": "trigger_once entry missing from docs manifest",
        }
        for classname, message in required.items():
            self.assertIn(classname, hlil_manifest, message)
            func_name = hlil_manifest[classname].get("function")
            self.assertTrue(
                isinstance(func_name, str) and func_name,
                f"{classname} should declare a function name",
            )


class Sub1000b150LogTest(unittest.TestCase):
    def test_logged_map_matches_snapshot(self) -> None:
        repo_root = Path(__file__).resolve().parents[1]
        script = repo_root / "tools" / "extract_spawn_manifest.py"
        expected_path = (
            repo_root
            / "references"
            / "HLIL"
            / "oblivion"
            / "interpreted"
            / "sub_1000b150_map.json"
        )
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir) / "map.json"
            subprocess.run(
                [
                    sys.executable,
                    str(script),
                    "--dump-b150-map",
                    str(output),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            current = json.loads(output.read_text(encoding="utf-8"))
        expected = json.loads(expected_path.read_text(encoding="utf-8"))
        self.assertEqual(
            current,
            expected,
            "sub_1000b150_map.json is out of date; rerun extract_spawn_manifest.py with --dump-b150-map",
        )


if __name__ == "__main__":
    unittest.main()
