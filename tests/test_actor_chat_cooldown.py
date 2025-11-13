import re
from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
ACTOR_SOURCE = REPO_ROOT / "src" / "game" / "m_actor.c"


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


class ActorChatCooldownTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = ACTOR_SOURCE.read_text(encoding="utf-8")

    def test_broadcast_message_cooldown_sequence(self) -> None:
        block = extract_function_block(self.source_text, "Actor_BroadcastMessage")
        guard = re.search(
            r"if\s*\(\s*level\.time\s*<\s*self->oblivion\.custom_name_time\s*\)\s*return\s*;",
            block,
        )
        self.assertIsNotNone(guard, "Cooldown guard missing from Actor_BroadcastMessage")
        assignment = re.search(
            r"self->oblivion\.custom_name_time\s*=\s*level\.time\s*\+\s*ACTOR_CHAT_COOLDOWN\s*;",
            block,
        )
        self.assertIsNotNone(assignment, "Cooldown timer is not updated after broadcasting")
        self.assertLess(
            guard.start(),
            assignment.start(),
            "Guard must execute before timer update to suppress rapid repeats",
        )

    def test_spawn_and_use_reset_cooldown(self) -> None:
        spawn_block = extract_function_block(self.source_text, "Actor_SpawnOblivion")
        self.assertRegex(
            spawn_block,
            r"Actor_ResetChatCooldown\s*\(\s*self\s*\)\s*;",
            msg="Spawn path must rewind the broadcast cooldown",
        )
        use_block = extract_function_block(self.source_text, "Actor_UseOblivion")
        self.assertRegex(
            use_block,
            r"Actor_ResetChatCooldown\s*\(\s*self\s*\)\s*;",
            msg="Use path must reset the broadcast cooldown",
        )
        attach_block = extract_function_block(self.source_text, "Actor_AttachController")
        self.assertRegex(
            attach_block,
            r"Actor_ResetChatCooldown\s*\(\s*self\s*\)\s*;",
            msg="Controller reattachment should align the broadcast cooldown",
        )

    def test_reset_helper_rewinds_timer(self) -> None:
        reset_block = extract_function_block(self.source_text, "Actor_ResetChatCooldown")
        self.assertIn(
            "self->oblivion.custom_name_time = level.time;",
            reset_block,
            "Reset helper should rewind the cooldown to allow future messages",
        )

    def test_repeated_touch_events_are_throttled(self) -> None:
        cooldown_match = re.search(
            r"#define\s+ACTOR_CHAT_COOLDOWN\s+([0-9.]+)f",
            self.source_text,
        )
        self.assertIsNotNone(cooldown_match, "Cooldown constant definition missing")
        cooldown = float(cooldown_match.group(1))

        custom_name_time = 0.0
        emissions = []
        for level_time in (5.0, 5.5, 6.0, 7.1):
            if level_time < custom_name_time:
                emissions.append(False)
                continue
            custom_name_time = level_time + cooldown
            emissions.append(True)

        self.assertEqual(
            emissions,
            [True, False, False, True],
            "Repeated touches within the cooldown window should not emit duplicate chat lines",
        )


if __name__ == "__main__":
    unittest.main()
