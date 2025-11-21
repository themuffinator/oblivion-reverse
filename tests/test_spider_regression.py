import json
import re
from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
SPIDER_SOURCE = REPO_ROOT / "src" / "game" / "m_spider.c"
FIXTURE_PATH = Path(__file__).resolve().parent / "fixtures" / "spider_expected_behaviour.json"

FRAME_DEFINE_RE = re.compile(r"#define\s+(SPIDER_FRAME_[A-Z0-9_]+)\s+([^\s]+)")
MMOVE_RE = re.compile(r"static\s+mmove_t\s+([a-z0-9_]+)\s*=\s*\{\s*([^}]+?)\s*\};", re.DOTALL)
SOUND_SETUP_RE = re.compile(r"sound_([a-z0-9_\[\]]+)\s*=\s*gi.soundindex\s*\(\"([^\"]+)\"\);")
VECTOR_SET_RE = re.compile(r"VectorSet\s*\(\s*self->(mins|maxs)\s*,\s*([-+0-9.]+)f?\s*,\s*([-+0-9.]+)f?\s*,\s*([-+0-9.]+)f?\s*\)\s*;")
COMBO_DEFINE_RE = re.compile(r"#define\s+(SPIDER_COMBO_[A-Z_]+)\s+([0-9.]+)f")


def load_fixture() -> dict:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))


def parse_frame_constants(source: str) -> dict:
    frames: dict[str, int] = {}
    for macro, raw_value in FRAME_DEFINE_RE.findall(source):
        frames[macro] = int(raw_value, 0)
    return frames


def parse_mmoves(source: str, frames: dict) -> dict:
    mmoves: dict[str, dict[str, int]] = {}

    def resolve_symbol(symbol: str) -> int:
        symbol = symbol.strip()
        if symbol in frames:
            return frames[symbol]
        if "+" in symbol:
            total = 0
            for part in symbol.split("+"):
                part = part.strip()
                if part in frames:
                    total += frames[part]
                else:
                    total += int(part, 0)
            return total
        return int(symbol, 0)

    for name, payload in MMOVE_RE.findall(source):
        parts = [part.strip() for part in payload.split(",") if part.strip()]
        if len(parts) < 2:
            continue
        start_symbol, end_symbol = parts[0], parts[1]
        start_value = resolve_symbol(start_symbol)
        end_value = resolve_symbol(end_symbol)
        mmoves[name] = {"start": start_value, "end": end_value}
    return mmoves


def extract_function_block(source: str, function_name: str) -> str:
    pattern = re.compile(rf"{function_name}\s*\([^)]*\)\s*\{{", re.MULTILINE)
    match = pattern.search(source)
    if not match:
        raise AssertionError(f"Function {function_name} not found in m_spider.c")
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


def parse_spawn_vectors(block: str) -> dict:
    vectors: dict[str, list[float]] = {}
    for match in VECTOR_SET_RE.finditer(block):
        key = match.group(1)
        coords = [float(part) for part in match.groups()[1:]]
        vectors[key] = coords
    return vectors


class SpiderRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = SPIDER_SOURCE.read_text(encoding="utf-8")
        cls.fixture = load_fixture()
        cls.frames = parse_frame_constants(cls.source_text)
        cls.mmoves = parse_mmoves(cls.source_text, cls.frames)

    def test_mmove_sequences_match_fixture(self) -> None:
        for name, expected in self.fixture["mmoves"].items():
            self.assertIn(name, self.mmoves, f"Missing mmove {name}")
            actual = self.mmoves[name]
            self.assertEqual(actual["start"], expected["start"], f"Start frame mismatch for {name}")
            self.assertEqual(actual["end"], expected["end"], f"End frame mismatch for {name}")

    def test_audio_table_matches_fixture(self) -> None:
        sounds: dict[str, list[str]] = {}
        for slot, sound_path in SOUND_SETUP_RE.findall(self.source_text):
            sounds.setdefault(slot, []).append(sound_path)

        audio_fixture = self.fixture["audio"]
        self.assertEqual(sounds.get("step", [None])[0], audio_fixture["step"])
        self.assertEqual(sounds.get("death", [None])[0], audio_fixture["death_voice"])
        self.assertEqual(sounds.get("death_thud", [None])[0], audio_fixture["death_thud"])
        pain_paths = sounds.get("pain1", []) + sounds.get("pain2", [])
        for path in audio_fixture["pain"]:
            self.assertIn(path, pain_paths, "Pain table missing expected clip")
        self.assertEqual(sounds.get("sight", [None])[0], audio_fixture["sight"])
        self.assertEqual(sounds.get("search", [None])[0], audio_fixture["search"])

    def test_boss_spawnflag_applies_hull_and_idle(self) -> None:
        spawn_block = extract_function_block(self.source_text, "SP_monster_spider")
        self.assertRegex(
            spawn_block,
            rf"spawnflags\s*&\s*0x{self.fixture['spawnflags']['boss_flag']:x}",
            "Boss spawnflag check missing",
        )
        vectors = parse_spawn_vectors(spawn_block)
        boss_hull = self.fixture["spawnflags"]["boss_hull"]
        self.assertEqual(vectors.get("mins"), boss_hull["mins"], "Boss mins hull mismatch")
        self.assertEqual(vectors.get("maxs"), boss_hull["maxs"], "Boss maxs hull mismatch")

    def test_combo_timing_constants_match_fixture(self) -> None:
        timings = {macro: float(value) for macro, value in COMBO_DEFINE_RE.findall(self.source_text)}
        combo_fixture = self.fixture["combo_timing"]
        self.assertAlmostEqual(timings.get("SPIDER_COMBO_FIRST_WINDOW", 0.0), combo_fixture["first"])
        self.assertAlmostEqual(timings.get("SPIDER_COMBO_CHAIN_WINDOW", 0.0), combo_fixture["chain"])
        self.assertAlmostEqual(timings.get("SPIDER_COMBO_FINISH_WINDOW", 0.0), combo_fixture["finish"])
        self.assertAlmostEqual(
            timings.get("SPIDER_COMBO_RECOVER_COOLDOWN", 0.0),
            combo_fixture["recover_cooldown"],
        )


if __name__ == "__main__":
    unittest.main()
