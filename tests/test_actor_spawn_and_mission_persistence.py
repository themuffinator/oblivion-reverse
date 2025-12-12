import re
from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
ACTOR_SOURCE = REPO_ROOT / "src" / "game" / "m_actor.c"
SAVE_SOURCE = REPO_ROOT / "src" / "game" / "g_save.c"
MISSION_SOURCE = REPO_ROOT / "src" / "game" / "g_mission.c"
TARGET_SOURCE = REPO_ROOT / "src" / "game" / "g_target.c"


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


class ActorSpawnAndMissionPersistenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.actor_text = ACTOR_SOURCE.read_text(encoding="utf-8")
        cls.save_text = SAVE_SOURCE.read_text(encoding="utf-8")
        cls.mission_text = MISSION_SOURCE.read_text(encoding="utf-8")
        cls.target_text = TARGET_SOURCE.read_text(encoding="utf-8")

    def test_spawn_defaults_force_targetname_and_start_on_use(self) -> None:
        helper_block = extract_function_block(self.actor_text, "Actor_ApplyTargetnameDefault")
        self.assertIn("Yo Mama", helper_block)
        self.assertIn("self->targetname && self->targetname[0]", helper_block)
        self.assertIn("self->targetname = (char *)kDefaultTargetName;", helper_block)
        self.assertIn("self->spawnflags |= ACTOR_SPAWNFLAG_START_ON;", helper_block)

        block = extract_function_block(self.actor_text, "Actor_SpawnOblivion")
        self.assertIn("Actor_ApplyTargetnameDefault(self);", block)
        self.assertRegex(
            block,
            r"spawnflags\s*&\s*ACTOR_SPAWNFLAG_START_ON[\s\S]*self->use\s*\(\s*self\s*,\s*world_ent\s*,\s*world_ent\s*\)\s*;",
            msg="START_ON actors should immediately invoke their use callback",
        )

    def test_path_think_reschedules_monster_loop(self) -> None:
        block = extract_function_block(self.actor_text, "Actor_PathThink")
        self.assertIn("self->think = Actor_PathThink;", block)
        self.assertRegex(block, r"self->nextthink\s*=\s*level\.time\s*\+\s*FRAMETIME\s*;")
        self.assertIn("monster_think(self);", block)

    def test_spawn_defaults_match_hlil_snapshot(self) -> None:
        block = extract_function_block(self.actor_text, "Actor_SpawnOblivion")
        self.assertRegex(
            block,
            r"self->s\.modelindex\s*=\s*0xff;[\s\S]*self->s\.modelindex2\s*=\s*0xff;",
            msg="Model indices should be seeded before linking",
        )
        self.assertRegex(
            block,
            r"self->speed\s*=\s*200;[\s\S]*self->mass\s*=\s*200;",
            msg="Spawn should force retail speed and mass defaults",
        )

        movement_block = extract_function_block(self.actor_text, "Actor_ConfigureMovementState")
        self.assertRegex(
            movement_block,
            r"VectorSet\(self->mins,\s*-16,\s*-16,\s*-24\);[\s\S]*VectorSet\(self->maxs,\s*16,\s*16,\s*32\);",
            msg="Movement state helper should configure the standing bounding box",
        )

    def test_spawn_ai_flags_match_hlil_snapshot(self) -> None:
        block = extract_function_block(self.actor_text, "Actor_ApplySpawnAIFeatures")
        self.assertRegex(
            block,
            r"target\)[\s\S]*AI_ACTOR_PATH_IDLE[\s\S]*else[\s\S]*AI_ACTOR_PATH_IDLE",
            msg="Path idle bit should reflect presence of an initial target",
        )
        self.assertIn("AI_STAND_GROUND", block)
        self.assertRegex(
            block,
            r"ACTOR_SPAWNFLAG_WIMPY[\s\S]*AI_ACTOR_FRIENDLY[\s\S]*AI_GOOD_GUY",
            msg="Friendly bits should clear for Wimpy actors and set otherwise",
        )

    def test_mission_fields_persisted_through_save_slots(self) -> None:
        self.assertRegex(
            self.save_text,
            r"\{\"mission_timer_limit\",\s*FOFS\(oblivion\.mission_timer_limit\),\s*F_INT\}",
            "Mission timer limit should be serialized",
        )
        self.assertRegex(
            self.save_text,
            r"\{\"mission_timer_start\",\s*FOFS\(oblivion\.mission_timer_remaining\),\s*F_INT\}",
            "Mission timer start/remaining should be serialized",
        )
        self.assertRegex(
            self.save_text,
            r"\{\"mission_flags\",\s*FOFS\(oblivion\.mission_timer_cooldown\),\s*F_INT\}",
            "Mission flag bitmask should be serialized",
        )

    def test_mission_timer_defaults_seed_remaining_time(self) -> None:
        actor_block = extract_function_block(self.actor_text, "Actor_InitMissionTimer")
        self.assertRegex(
            actor_block,
            r"mission_timer_limit\s*<\s*0\)[^\n]*\n\s*self->oblivion\.mission_timer_limit\s*=\s*0;",
            "Actor timers should clamp negative limits to zero",
        )
        self.assertRegex(
            actor_block,
            r"mission_timer_remaining\s*<=\s*0[\s\S]*mission_timer_limit\s*>\s*0[\s\S]*mission_timer_remaining\s*=\s*self->oblivion\.mission_timer_limit",
            "Actor timers should seed remaining time from the limit",
        )

        mission_block = extract_function_block(self.mission_text, "Mission_RegisterHelpTarget")
        self.assertRegex(
            mission_block,
            r"mission_timer_cooldown\s*\|=\s*MISSION_FLAG_PRIMARY",
            "Mission registration should mirror spawn flag primary bit",
        )
        self.assertRegex(
            mission_block,
            r"mission_timer_remaining\s*=\s*ent->oblivion\.mission_timer_limit;",
            "Mission registration should seed countdowns from the limit when missing",
        )

    def test_path_chat_and_mission_loops_broadcast_to_all_clients(self) -> None:
        message_block = extract_function_block(self.actor_text, "Actor_BroadcastMessage")
        self.assertRegex(
            message_block,
            r"for\s*\(\s*i\s*=\s*1;\s*i\s*<=\s*game\.maxclients;\s*i\+\+\s*\)[\s\S]*gi\.cprintf",
            msg="Actor messages should loop over every active client",
        )

    def test_target_help_broadcasts_to_all_clients(self) -> None:
        block = extract_function_block(self.target_text, "Use_Target_Help")
        self.assertRegex(
            block,
            r"for\s*\(\s*i\s*=\s*1;\s*i\s*<=\s*game\.maxclients;\s*i\+\+\s*\)[\s\S]*if\s*\(!client->inuse\)[\s\S]*gi\.cprintf",
            "Use handler should broadcast messages to every active client",
        )

    def test_mission_completion_marks_unread_queue(self) -> None:
        block = extract_function_block(self.mission_text, "Mission_TargetHelpFired")
        self.assertIn("obj->state = MISSION_OBJECTIVE_COMPLETED;", block)
        self.assertIn("Mission_MarkUnread ();", block)


if __name__ == "__main__":
    unittest.main()
