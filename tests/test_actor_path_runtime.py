import re
from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
ACTOR_SOURCE = REPO_ROOT / "src" / "game" / "m_actor.c"


def extract_function_block(source: str, function_name: str) -> str:
    escaped = re.escape(function_name)
    pattern = re.compile(r"{}\\s*\\([^)]*\\)\\s*\\{{".format(escaped), re.MULTILINE)
    match = pattern.search(source)
    if not match:
        secondary = re.compile(r"{}\s*\(".format(escaped), re.MULTILINE)
        alt_match = secondary.search(source)
        while alt_match:
            token_index = alt_match.start()
            prefix_ok = token_index == 0 or not (source[token_index - 1].isalnum() or source[token_index - 1] == "_")
            if prefix_ok:
                match_start = source.rfind("\n", 0, token_index)
                if match_start == -1:
                    match_start = 0
                else:
                    match_start += 1
                brace_index = source.find("{", alt_match.end())
                if brace_index != -1:
                    start = brace_index + 1
                    depth = 1
                    idx = start
                    while idx < len(source) and depth > 0:
                        char = source[idx]
                        if char == "{":
                            depth += 1
                        elif char == "}":
                            depth -= 1
                        idx += 1
                    return source[match_start:idx]
            alt_match = secondary.search(source, alt_match.end())
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

    def test_spawn_sets_prethink_and_resets_path(self) -> None:
        block = extract_function_block(self.source_text, "Actor_SpawnOblivion")
        self.assertIn("self->prethink = Actor_PreThink;", block)
        self.assertIn("Actor_PathResetState(self);", block)
        self.assertIn("Actor_InitMissionTimer(self);", block)

    def test_use_resets_controller_state(self) -> None:
        block = extract_function_block(self.source_text, "Actor_UseOblivion")
        self.assertIn("self->oblivion.prev_path = NULL;", block)
        self.assertIn("Actor_PathAssignController(self, NULL);", block)
        self.assertIn("Actor_UpdateMissionObjective(self);", block)

    def test_target_actor_advances_path_state(self) -> None:
        block = extract_function_block(self.source_text, "target_actor_touch")
        self.assertIn("wait = Actor_PathResolveWait(other, self);", block)
        self.assertIn("Actor_PathAdvance(other, self, next_target);", block)
        self.assertIn("Actor_PathApplyWait(other, wait);", block)
        self.assertIn("Actor_UpdateMissionObjective(other);", block)

    def test_prethink_tracks_controller_and_mission(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PreThink")
        self.assertIn("Actor_PathTrackController(self);", block)
        self.assertRegex(
            block,
            r"self->oblivion.path_state\s*==\s*ACTOR_PATH_STATE_WAITING",
        )
        self.assertIn("Actor_UpdateMissionObjective(self);", block)


if __name__ == "__main__":
    unittest.main()
