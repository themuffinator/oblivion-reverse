import sys
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.extract_spawn_manifest import HLILParser


class HLILParserSpawnMapStrcmpTest(unittest.TestCase):
    def test_extracts_spawn_entries_from_strcmp_chains(self) -> None:
        parser = HLILParser(Path("references/HLIL/oblivion/gamex86.dll_hlil.txt"))

        block = parser.function_blocks["sub_10005e30"]
        monster_entries = parser._extract_spawn_map_from_strcmp(block)

        expected_monsters = {
            "monster_tank": "sub_10001ac0",
            "monster_supertank": "sub_10001ac0",
            "monster_makron": "sub_10001ac0",
            "monster_jorg": "sub_10001ac0",
        }
        for classname, func in expected_monsters.items():
            self.assertIn(classname, monster_entries)
            self.assertEqual(monster_entries[classname], func)

        spawn_map = parser.spawn_map
        for classname, func in expected_monsters.items():
            self.assertIn(classname, spawn_map)
            self.assertEqual(spawn_map[classname], func)

        func_entries = parser._extract_spawn_map_from_strcmp(parser.function_blocks["sub_100090f0"])
        expected_funcs = {
            "func_door": "sub_10006b20",
            "func_door_rotating": "sub_10006df0",
        }
        for classname, func in expected_funcs.items():
            self.assertIn(classname, func_entries)
            self.assertEqual(func_entries[classname], func)
            self.assertIn(classname, spawn_map)
            self.assertEqual(spawn_map[classname], func)

    def test_extracts_spawn_entries_with_nested_gotos(self) -> None:
        parser = HLILParser(Path("references/HLIL/oblivion/gamex86.dll_hlil.txt"))

        block = parser.function_blocks["sub_10031d70"]
        entries = parser._extract_spawn_map_from_strcmp(block)

        self.assertIn("misc_actor", entries)
        self.assertEqual(entries["misc_actor"], "sub_10037cd0")

        spawn_map = parser.spawn_map
        self.assertIn("misc_actor", spawn_map)
        self.assertEqual(spawn_map["misc_actor"], "sub_10037cd0")


if __name__ == "__main__":
    unittest.main()
