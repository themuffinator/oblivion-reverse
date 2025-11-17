import re
import json
import re
from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
KIGRAX_SOURCE = REPO_ROOT / "src" / "game" / "m_kigrax.c"
FIXTURE_PATH = Path(__file__).resolve().parent / "fixtures" / "kigrax_expected_spawn.json"

FRAME_DEFINE_RE = re.compile(r"#define\s+(KIGRAX_FRAME_[A-Z0-9_]+)\s+([^\s]+)")
ENUM_FRAME_RE = re.compile(r"(KIGRAX_FRAME_[A-Z0-9_]+)\s*=\s*([^,\n]+)")
MMOVE_RE = re.compile(
    r"static\s+mmove_t\s+([a-z0-9_]+)\s*=\s*\{\s*([^}]+?)\s*\};",
    re.DOTALL,
)
VECTOR_SET_RE = re.compile(
    r"VectorSet\s*\(\s*self->(mins|maxs)\s*,\s*([-+0-9.]+)f?\s*,\s*([-+0-9.]+)f?\s*,\s*([-+0-9.]+)f?\s*\)\s*;",
    re.IGNORECASE,
)
MOVETYPE_MAP = {
    "MOVETYPE_STEP": 5.0,
}


def load_spawn_fixture() -> dict:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))


def parse_frame_constants(source: str) -> dict:
    frames: dict[str, int] = {}
    for macro, raw_value in FRAME_DEFINE_RE.findall(source):
        if raw_value.lower().startswith("0x"):
            frames[macro] = int(raw_value, 16)
        else:
            frames[macro] = int(raw_value)
    for macro, raw_value in ENUM_FRAME_RE.findall(source):
        cleaned = raw_value.strip()
        if cleaned.lower().startswith("0x"):
            frames[macro] = int(cleaned, 16)
        else:
            frames[macro] = int(cleaned)
    return frames


def parse_mmoves(source: str, frames: dict) -> dict:
    mmoves: dict[str, dict[str, int]] = {}
    for name, payload in MMOVE_RE.findall(source):
        parts = [part.strip() for part in payload.split(",") if part.strip()]
        if len(parts) < 2:
            continue
        start_symbol, end_symbol = parts[0], parts[1]
        start_value = frames.get(start_symbol, None)
        end_value = frames.get(end_symbol, None)
        if start_value is None:
            start_value = int(start_symbol, 0)
        if end_value is None:
            end_value = int(end_symbol, 0)
        mmoves[name] = {"start": start_value, "end": end_value}
    return mmoves


def extract_function_block(source: str, function_name: str) -> str:
    pattern = re.compile(rf"{function_name}\s*\([^)]*\)\s*\{{", re.MULTILINE)
    match = pattern.search(source)
    if not match:
        raise AssertionError(f"Function {function_name} not found in m_kigrax.c")
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


def extract_spawn_defaults(source: str) -> dict[str, float]:
    block = extract_function_block(source, "SP_monster_kigrax")
    defaults: dict[str, float] = {}
    for match in VECTOR_SET_RE.finditer(block):
        vec_name = match.group(1).lower()
        values = [float(part) for part in match.groups()[1:]]
        if vec_name == "mins":
            offsets = ("offset_0xbc", "offset_0xc0", "offset_0xc4")
        else:
            offsets = ("offset_0xc8", "offset_0xcc", "offset_0xd0")
        for offset, value in zip(offsets, values):
            defaults[offset] = value

    movetype_match = re.search(r"self->movetype\s*=\s*(MOVETYPE_[A-Z_]+)", block)
    if movetype_match:
        key = movetype_match.group(1)
        defaults["offset_0xf8"] = MOVETYPE_MAP.get(key, float("nan"))

    yaw_match = re.search(r"self->yaw_speed\s*=\s*([-+0-9.]+)f?", block)
    if yaw_match:
        defaults["offset_0x1e4"] = float(yaw_match.group(1))

    viewheight_match = re.search(r"self->viewheight\s*=\s*([-+0-9.]+)", block)
    if viewheight_match:
        defaults["offset_0x23c"] = float(viewheight_match.group(1))

    return defaults


class KigraxDeathRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = KIGRAX_SOURCE.read_text(encoding="utf-8")

    def test_corpse_switches_to_toss_and_explodes(self) -> None:
        dead_block = extract_function_block(self.source_text, "kigrax_dead")
        self.assertIn(
            "self->movetype = MOVETYPE_TOSS;",
            dead_block,
            "Kigrax corpse must swap to MOVETYPE_TOSS when the death mmove finishes",
        )
        self.assertIn(
            "self->think = kigrax_deadthink;",
            dead_block,
            "Kigrax corpse should install the hover-style explosion thinker",
        )
        self.assertIn(
            "self->nextthink = level.time + FRAMETIME;",
            dead_block,
            "Corpse toss needs to schedule the timed thinker so the explosion triggers",
        )
        self.assertIn(
            "self->timestamp = level.time + 15.0f;",
            dead_block,
            "Corpse explosion timeout deviates from the hover baseline",
        )

        think_block = extract_function_block(self.source_text, "kigrax_deadthink")
        self.assertIn(
            "BecomeExplosion1 (self);",
            think_block,
            "Explosion thinker no longer detonates the corpse",
        )
        self.assertRegex(
            think_block,
            r"level.time\s*\+\s*FRAMETIME",
            "Explosion thinker must keep scheduling itself until the corpse lands",
        )


class KigraxSpawnManifestRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = KIGRAX_SOURCE.read_text(encoding="utf-8")
        cls.fixture = load_spawn_fixture()
        cls.frame_constants = parse_frame_constants(cls.source_text)
        cls.mmoves = parse_mmoves(cls.source_text, cls.frame_constants)

    def test_spawn_defaults_match_fixture(self) -> None:
        expected = self.fixture["defaults"]
        parsed = extract_spawn_defaults(self.source_text)
        for key, expected_value in expected.items():
            self.assertIn(key, parsed, f"Missing spawn default {key}")
            self.assertAlmostEqual(
                parsed[key],
                expected_value,
                places=3,
                msg=f"Spawn default {key} diverged from HLIL fixture",
            )

    def test_mmove_sequences_match_fixture(self) -> None:
        expected_sequences = self.fixture["mmoves"]
        for mmove_name, expected_range in expected_sequences.items():
            self.assertIn(mmove_name, self.mmoves, f"Missing mmove {mmove_name}")
            actual = self.mmoves[mmove_name]
            self.assertEqual(
                actual["start"],
                expected_range["start"],
                f"Start frame mismatch for {mmove_name}",
            )
            self.assertEqual(
                actual["end"],
                expected_range["end"],
                f"End frame mismatch for {mmove_name}",
            )


if __name__ == "__main__":
    unittest.main()
