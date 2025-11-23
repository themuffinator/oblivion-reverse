import json
import re
from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
CYBORG_SOURCE = REPO_ROOT / "src" / "game" / "m_cyborg.c"
FIXTURE_PATH = Path(__file__).resolve().parent / "fixtures" / "cyborg_expected_behaviour.json"


FRAME_DEFINE_RE = re.compile(r"#define\s+(CYBORG_FRAME_[A-Z0-9_]+)\s+([^\s]+)")
MMOVE_RE = re.compile(
    r"static\s+mmove_t\s+([a-z0-9_]+)\s*=\s*\{\s*([^}]+?)\s*\};",
    re.DOTALL,
)
ATTACK_THRESHOLD_RE = re.compile(r"choice\s*<\s*([0-9]+\.[0-9]+)f")
CURRENTMOVE_ASSIGN_RE = re.compile(r"self->monsterinfo.currentmove\s*=\s*&([a-z0-9_]+);")
PAIN_COOLDOWN_RE = re.compile(r"self->pain_debounce_time\s*=\s*level.time\s*\+\s*([0-9]+\.[0-9]+)f;")
ATTACK_FINISHED_RE = re.compile(
    r"level.time\s*\+\s*([0-9]+\.[0-9]+)f\s*\+\s*random\s*\(\)\s*\*\s*([0-9]+\.[0-9]+)f"
)
SOUND_SETUP_RE = re.compile(
    r"sound_([a-z0-9_\[\]]+)\s*=\s*gi.soundindex\s*\(\"([^\"]+)\"\);"
)
LAND_HELPER_CALL_RE = re.compile(r"cyborg_land\s*\(\s*self\s*\)\s*;")
LANDING_FLAG_SET_RE = re.compile(r"self->oblivion\.cyborg_landing_thud\s*=\s*true\s*;")


def load_fixture() -> dict:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))


def parse_frame_constants(source: str) -> dict:
    frames: dict[str, int] = {}
    for macro, raw_value in FRAME_DEFINE_RE.findall(source):
        if raw_value.lower().startswith("0x"):
            frames[macro] = int(raw_value, 16)
        else:
            frames[macro] = int(raw_value)
    return frames


def parse_mmoves(source: str, frames: dict) -> dict:
    mmoves: dict[str, dict[str, int]] = {}
    for name, payload in MMOVE_RE.findall(source):
        parts = [part.strip() for part in payload.split(",") if part.strip()]
        if len(parts) < 2:
            continue
        start_symbol, end_symbol = parts[0], parts[1]
        endfunc = parts[3] if len(parts) > 3 else None
        start_value = frames.get(start_symbol, None)
        end_value = frames.get(end_symbol, None)
        if start_value is None:
            start_value = int(start_symbol, 0)
        if end_value is None:
            end_value = int(end_symbol, 0)
        mmoves[name] = {"start": start_value, "end": end_value}
        if endfunc:
            mmoves[name]["endfunc"] = endfunc
    return mmoves


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


class CyborgRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = CYBORG_SOURCE.read_text(encoding="utf-8")
        cls.fixture = load_fixture()
        cls.frame_constants = parse_frame_constants(cls.source_text)
        cls.mmoves = parse_mmoves(cls.source_text, cls.frame_constants)

    def test_frame_sequences_match_fixture(self) -> None:
        expected_sequences = self.fixture["frame_sequences"]
        for mmove_name, expected in expected_sequences.items():
            self.assertIn(mmove_name, self.mmoves, f"Missing mmove {mmove_name}")
            actual = self.mmoves[mmove_name]
            self.assertEqual(
                actual["start"],
                expected["start"],
                f"Start frame mismatch for {mmove_name}",
            )
            self.assertEqual(
                actual["end"],
                expected["end"],
                f"End frame mismatch for {mmove_name}",
            )

    def test_attack_dispatch_thresholds_and_variants(self) -> None:
        block = extract_function_block(self.source_text, "cyborg_attack_dispatch")
        thresholds = [float(value) for value in ATTACK_THRESHOLD_RE.findall(block)]
        self.assertEqual(
            thresholds,
            self.fixture["attack_thresholds"],
            "Attack thresholds diverged from the expected fixture",
        )
        assignments = CURRENTMOVE_ASSIGN_RE.findall(block)
        variants = [name for name in assignments if name.startswith("cyborg_move_attack")]
        self.assertEqual(
            variants,
            self.fixture["expected_attack_sequence"],
            "Attack mmove dispatch order no longer matches the captured baseline",
        )
        for variant in variants:
            self.assertEqual(
                self.mmoves[variant].get("endfunc"),
                "cyborg_locomotion_stage",
                f"Attack mmove {variant} no longer routes back through cyborg_locomotion_stage",
            )

    def test_pain_cooldown_matches_fixture(self) -> None:
        block = extract_function_block(self.source_text, "cyborg_pain")
        match = PAIN_COOLDOWN_RE.search(block)
        self.assertIsNotNone(match, "Pain cooldown assignment missing")
        cooldown = float(match.group(1))
        self.assertAlmostEqual(
            cooldown,
            self.fixture["pain_cooldown"],
            msg="Pain cooldown changed – update the fixture or investigate regression",
        )

    def test_pain_handler_triggers_wounded_anchor(self) -> None:
        block = extract_function_block(self.source_text, "cyborg_pain")
        self.assertRegex(
            block,
            r"cyborg_wound_stand_ground\s*\(\s*self\s*\)",
            "Pain handler must update the wounded stand-ground anchoring",
        )

        debounce_assign = "self->oblivion.cyborg_pain_time = self->pain_debounce_time;"
        self.assertIn(
            debounce_assign,
            block,
            "Pain handler should still assign the wound debounce timer",
        )
        self.assertGreater(
            block.index("cyborg_wound_stand_ground"),
            block.index(debounce_assign),
            "Wounded anchor update must run after the debounce check succeeds",
        )

    def test_attack_finished_timer_and_stand_ground(self) -> None:
        block = extract_function_block(self.source_text, "cyborg_attack_dispatch")
        match = ATTACK_FINISHED_RE.search(block)
        self.assertIsNotNone(match, "Attack finished timer not found")
        base, random_scale = map(float, match.groups())
        self.assertAlmostEqual(base, self.fixture["stand_ground"]["attack_finished_base"])
        self.assertAlmostEqual(
            random_scale,
            self.fixture["stand_ground"]["attack_finished_random"],
        )
        self.assertIn("cyborg_stand", block, "Stand-ground branch missing from attack dispatch")

    def test_audio_configuration_matches_fixture(self) -> None:
        sounds: dict[str, list[str]] = {}
        for slot, sound_path in SOUND_SETUP_RE.findall(self.source_text):
            sounds.setdefault(slot, []).append(sound_path)
        audio_fixture = self.fixture["audio_cues"]
        self.assertEqual(
            sounds.get("attack", [None])[0],
            audio_fixture["attack"],
            "Attack sound changed unexpectedly",
        )
        self.assertListEqual(
            sorted(sounds.get("step[0]", []) + sounds.get("step[1]", []) + sounds.get("step[2]", [])),
            sorted(audio_fixture["steps"]),
            "Footstep set diverged from fixture",
        )
        self.assertIn(
            audio_fixture["landing_thud"],
            audio_fixture["steps"],
            "Landing thud placeholder no longer maps to one of the configured step cues",
        )
        pain_sounds = sounds.get("pain", [])
        self.assertTrue(pain_sounds, "Pain sound not configured in spawn function")
        for sound in pain_sounds:
            self.assertIn(sound, audio_fixture["pain"])

    def test_landing_thud_helper_wiring(self) -> None:
        landing_block = extract_function_block(self.source_text, "cyborg_land")
        self.assertIn(
            "sound_thud",
            landing_block,
            "Landing helper no longer references the heavy impact cue",
        )
        self.assertIn(
            "CHAN_BODY",
            landing_block,
            "Landing helper must emit the thud on the body channel",
        )

        for function_name in (
            "cyborg_locomotion_stage",
            "cyborg_locomotion_resume",
            "cyborg_update_stand_ground",
        ):
            block = extract_function_block(self.source_text, function_name)
            self.assertRegex(
                block,
                LAND_HELPER_CALL_RE,
                msg=f"{function_name} no longer routes through cyborg_land",
            )

        schedule_block = extract_function_block(
            self.source_text, "cyborg_schedule_stand_ground"
        )
        self.assertRegex(
            schedule_block,
            LANDING_FLAG_SET_RE,
            msg="Wounded anchor helper must arm the landing thud flag",
        )

        attack_block = extract_function_block(self.source_text, "cyborg_attack_dispatch")
        self.assertGreaterEqual(
            len(LANDING_FLAG_SET_RE.findall(attack_block)),
            3,
            "Attack dispatch must set the landing thud flag for each variant",
        )

    def test_simulated_combat_timeline_from_fixture(self) -> None:
        fixture = self.fixture["simulation"]
        thresholds = self.fixture["attack_thresholds"]
        variants = self.fixture["expected_attack_sequence"]
        chosen_variants = []
        for value in fixture["random_values"]:
            if value < thresholds[0]:
                chosen_variants.append(variants[0])
            elif value < thresholds[1]:
                chosen_variants.append(variants[1])
            else:
                chosen_variants.append(variants[2])
        self.assertEqual(
            chosen_variants,
            variants,
            "Attack randomisation no longer yields the expected variant rotation",
        )

        cooldown = self.fixture["pain_cooldown"]
        events = []
        last_pain = -1e9
        pain_sound = self.fixture["audio_cues"]["pain"][0]
        for timestamp in fixture["pain_timestamps"]:
            if timestamp >= last_pain + cooldown:
                events.append({"event": "pain", "sound": pain_sound})
                last_pain = timestamp
        self.assertEqual(
            len([event for event in events if event["event"] == "pain"]),
            fixture["expected_pain_events"],
            "Pain cooldown timeline diverged from fixture expectations",
        )

        attack_event = {"event": "attack", "sound": self.fixture["audio_cues"]["attack"]}
        landing_event = {
            "event": "landing_thud",
            "sound": self.fixture["audio_cues"]["landing_thud"],
        }
        timeline = [attack_event] + events + [landing_event]
        self.assertEqual(
            timeline,
            fixture["expected_audio_timeline"],
            "Combined combat timeline no longer matches captured expectations",
        )

    def test_ai_timeline_progression_matches_fixture(self) -> None:
        ordered = [
            "cyborg_move_walk",
            "cyborg_move_attack_primary",
            "cyborg_move_attack_secondary",
            "cyborg_move_attack_barrage",
            "cyborg_move_pain_stagger",
            "cyborg_move_pain_recover",
            "cyborg_move_idle",
            "cyborg_move_stand",
        ]
        last_end = -1
        for name in ordered:
            self.assertIn(name, self.mmoves, f"Missing mmove {name} in timeline check")
            expected = self.fixture["frame_sequences"][name]
            actual = self.mmoves[name]
            self.assertEqual(actual["start"], expected["start"])
            self.assertEqual(actual["end"], expected["end"])
            self.assertGreater(
                actual["start"],
                last_end,
                f"{name} starts before the prior mmove ended; AI timeline ordering regressed",
            )
            last_end = actual["end"]


if __name__ == "__main__":
    unittest.main()
