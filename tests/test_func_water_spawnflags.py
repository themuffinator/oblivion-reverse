import re
import unittest
import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
G_FUNC_SOURCE = REPO_ROOT / "src" / "game" / "g_func.c"


def extract_function_block(source: str, function_name: str) -> str:
    pattern = re.compile(rf"{function_name}\s*\([^)]*\)\s*\{{", re.MULTILINE)
    match = pattern.search(source)
    if not match:
        raise AssertionError(f"Could not locate function {function_name}")
    start = match.end()
    depth = 1
    idx = start
    while idx < len(source) and depth > 0:
        char = source[idx]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        idx += 1
    return source[match.start():idx]


class FuncWaterSpawnflagTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = G_FUNC_SOURCE.read_text(encoding="utf-8")
        cls.func_block = extract_function_block(cls.source_text, "SP_func_water")

    def test_wait_defaults_preserve_non_toggle_behavior(self) -> None:
        self.assertRegex(
            self.func_block,
            r"if\s*\(\s*self->wait\s*<\s*0\s*\)\s*self->wait\s*=\s*-1",
            "Negative waits should still clamp to -1",
        )
        self.assertNotRegex(
            self.func_block,
            r"if\s*\(\s*!self->wait",
            "func_water must not treat a missing wait key as -1",
        )

    def test_toggle_assignment_guarded_by_wait_check(self) -> None:
        toggle_pattern = re.compile(
            r"if\s*\(\s*self->wait\s*==\s*-1\s*\)[^}]*self->spawnflags\s*\|=\s*DOOR_TOGGLE",
            re.DOTALL,
        )
        self.assertRegex(
            self.func_block,
            toggle_pattern,
            "DOOR_TOGGLE should only be set when wait stays -1",
        )


if __name__ == "__main__":
    unittest.main()
