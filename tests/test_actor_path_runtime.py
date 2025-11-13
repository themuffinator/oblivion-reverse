import re
import unittest
from pathlib import Path

ACTOR_SOURCE = Path(__file__).resolve().parents[1] / "src" / "game" / "m_actor.c"


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


class ActorPathRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = ACTOR_SOURCE.read_text(encoding="utf-8")

    def test_actor_use_oblivion_attaches_controller(self) -> None:
        block = extract_function_block(self.source_text, "Actor_UseOblivion")
        self.assertIn("Actor_AttachController(self, target)", block)
        self.assertIn("Actor_FireMissionEvent(self, activator)", block)
        self.assertIn("Actor_PathBind(self, NULL)", block)

    def test_target_actor_touch_updates_path_state(self) -> None:
        block = extract_function_block(self.source_text, "target_actor_touch")
        self.assertIn("other->oblivion.prev_path = self;", block)
        self.assertIn("Actor_PathBind(other, next_target);", block)
        self.assertIn("Actor_PathMarkWait(other, self->wait);", block)
        self.assertIn("other->oblivion.script_target = pathtarget_ent;", block)

    def test_spawn_enables_oblivion_think(self) -> None:
        block = extract_function_block(self.source_text, "Actor_SpawnOblivion")
        self.assertIn("Actor_PathBind(self, NULL);", block)
        self.assertIn("Actor_PathMarkWait(self, -1.0f);", block)
        self.assertIn("Actor_EnableOblivionThink(self);", block)

    def test_oblivion_think_wraps_monster_think(self) -> None:
        block = extract_function_block(self.source_text, "Actor_OblivionThink")
        self.assertIn("monster_think(self);", block)
        self.assertIn("Actor_PathSync(self);", block)


if __name__ == "__main__":
    unittest.main()
