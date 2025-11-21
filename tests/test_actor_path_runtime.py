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
                semicolon_index = source.find(";", alt_match.end())
                if semicolon_index != -1 and semicolon_index < brace_index:
                    alt_match = secondary.search(source, alt_match.end())
                    continue
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

    def test_use_schedules_path_think(self) -> None:
        block = extract_function_block(self.source_text, "Actor_UseOblivion")
        self.assertIn("self->think = Actor_PathThink;", block)
        self.assertIn("self->nextthink = level.time + FRAMETIME;", block)

    def test_actor_use_wires_think(self) -> None:
        block = extract_function_block(self.source_text, "actor_use")
        self.assertIn("self->think = Actor_PathThink;", block)
        self.assertIn("self->nextthink = level.time + FRAMETIME;", block)

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

    def test_path_select_idle_randomises(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PathSelectIdleAnimation")
        self.assertIn("rand() % 3", block)
        self.assertIn("self->monsterinfo.pausetime = level.time + delay;", block)

    def test_path_schedule_idle_sets_flag(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PathScheduleIdle")
        self.assertIn("self->monsterinfo.pausetime = level.time + delay;", block)
        self.assertIn("self->monsterinfo.aiflags |= AI_ACTOR_PATH_IDLE;", block)

    def test_path_reconcile_updates_goal(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PathReconcileTargets")
        self.assertIn("self->goalentity = self->oblivion.controller;", block)
        self.assertIn("self->oblivion.controller_serial = self->oblivion.controller->count;", block)

    def test_path_think_calls_monster_think(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PathThink")
        self.assertIn("self->think = Actor_PathThink;", block)
        self.assertIn("monster_think(self);", block)

    def test_path_apply_wait_schedules_idle(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PathApplyWait")
        self.assertIn("Actor_PathScheduleIdle(self);", block)

    def test_path_advance_updates_toggle_and_serial(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PathAdvance")
        self.assertIn("self->oblivion.path_toggle ^= 1;", block)
        self.assertIn("self->oblivion.controller_serial = next_target ? next_target->count : 0;", block)

    def test_path_assign_controller_sets_serial(self) -> None:
        block = extract_function_block(self.source_text, "Actor_PathAssignController")
        self.assertIn("self->oblivion.controller_serial = controller ? controller->count : 0;", block)
        self.assertIn("Actor_PathScheduleIdle(self);", block)

    def test_actor_walk_updates_state(self) -> None:
        block = extract_function_block(self.source_text, "actor_walk")
        self.assertIn("self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;", block)
        self.assertIn("self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;", block)

    def test_actor_run_updates_state(self) -> None:
        block = extract_function_block(self.source_text, "actor_run")
        self.assertIn("self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;", block)
        self.assertIn("self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;", block)

    def test_path_fields_persisted_in_save(self) -> None:
        save_text = (REPO_ROOT / "src" / "game" / "g_save.c").read_text(encoding="utf-8")
        for token in (
            "mission_path_target",
            "mission_script_target",
            "mission_path_toggle",
            "mission_path_wait",
            "mission_path_time",
        ):
            self.assertIn(token, save_text)


if __name__ == "__main__":
    unittest.main()
