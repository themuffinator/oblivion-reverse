import re
from copy import deepcopy
from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
G_SAVE_PATH = REPO_ROOT / "src" / "game" / "g_save.c"

MISSION_FIELD_RE = re.compile(
    r"\{\s*\"(?P<name>mission_[^\"]+)\"\s*,\s*FOFS\(oblivion\.[^)]*\)\s*,\s*(?P<type>F_[A-Z_]+)",
)


class MissionSaveRegressionTest(unittest.TestCase):
    def _parse_mission_fields(self) -> dict[str, str]:
        save_source = G_SAVE_PATH.read_text(encoding="utf-8")
        return {match.group("name"): match.group("type") for match in MISSION_FIELD_RE.finditer(save_source)}

    def _snapshot_fields(self, entity: dict, mission_fields: dict[str, str], index_map: dict[int, int]) -> dict[str, object]:
        snapshot: dict[str, object] = {}
        for name, field_type in mission_fields.items():
            value = entity.get(name)
            if field_type == "F_VECTOR":
                snapshot[name] = tuple(value) if value is not None else None
            elif field_type == "F_EDICT":
                snapshot[name] = index_map.get(id(value), -1) if value is not None else -1
            else:
                snapshot[name] = deepcopy(value)
        return snapshot

    def _serialize_entity(self, entity: dict, mission_fields: dict[str, str], index_map: dict[int, int]) -> dict[str, object]:
        serialized: dict[str, object] = {}
        for name, field_type in mission_fields.items():
            value = entity.get(name)
            if field_type == "F_VECTOR":
                serialized[name] = list(value) if value is not None else None
            elif field_type == "F_EDICT":
                serialized[name] = index_map.get(id(value), -1) if value is not None else -1
            else:
                serialized[name] = deepcopy(value)
        return serialized

    def _restore_entity(self, serialized: dict[str, object], mission_fields: dict[str, str], lookup: dict[int, dict]) -> dict:
        restored: dict[str, object] = {}
        for name, field_type in mission_fields.items():
            value = serialized.get(name)
            if field_type == "F_VECTOR":
                restored[name] = tuple(value) if value is not None else None
            elif field_type == "F_EDICT":
                restored[name] = lookup.get(value) if isinstance(value, int) and value >= 0 else None
            else:
                restored[name] = deepcopy(value)
        return restored

    def test_mission_fields_round_trip_through_save(self) -> None:
        mission_fields = self._parse_mission_fields()

        expected_names = {
            "mission_controller",
            "mission_last_controller",
            "mission_prev_path",
            "mission_path_target",
            "mission_script_target",
            "mission_controller_serial",
            "mission_path_toggle",
            "mission_controller_distance",
            "mission_controller_resume",
            "mission_path_wait",
            "mission_path_time",
            "mission_path_speed",
            "mission_path_step_speed",
            "mission_path_remaining",
            "mission_path_state",
            "mission_path_dir",
            "mission_path_velocity",
            "mission_custom_name",
            "mission_custom_name_time",
            "mission_id",
            "mission_title",
            "mission_text",
            "mission_event",
            "mission_timer_limit",
            "mission_timer_start",
            "mission_flags",
            "mission_origin",
            "mission_angles",
            "mission_velocity",
            "mission_blend",
            "mission_radius",
        }
        self.assertTrue(
            expected_names.issubset(mission_fields.keys()),
            "Missing mission path or objective fields from g_save serialization",
        )

        expected_types = {
            "mission_controller": "F_EDICT",
            "mission_last_controller": "F_EDICT",
            "mission_prev_path": "F_EDICT",
            "mission_path_target": "F_EDICT",
            "mission_script_target": "F_EDICT",
            "mission_controller_serial": "F_INT",
            "mission_path_toggle": "F_INT",
            "mission_controller_distance": "F_FLOAT",
            "mission_controller_resume": "F_FLOAT",
            "mission_path_wait": "F_FLOAT",
            "mission_path_time": "F_FLOAT",
            "mission_path_speed": "F_FLOAT",
            "mission_path_step_speed": "F_FLOAT",
            "mission_path_remaining": "F_FLOAT",
            "mission_path_state": "F_INT",
            "mission_path_dir": "F_VECTOR",
            "mission_path_velocity": "F_VECTOR",
            "mission_custom_name": "F_LSTRING",
            "mission_custom_name_time": "F_FLOAT",
            "mission_id": "F_LSTRING",
            "mission_title": "F_LSTRING",
            "mission_text": "F_LSTRING",
            "mission_event": "F_INT",
            "mission_timer_limit": "F_INT",
            "mission_timer_start": "F_INT",
            "mission_flags": "F_INT",
            "mission_origin": "F_VECTOR",
            "mission_angles": "F_VECTOR",
            "mission_velocity": "F_VECTOR",
            "mission_blend": "F_FLOAT",
            "mission_radius": "F_FLOAT",
        }
        for name, expected_type in expected_types.items():
            self.assertEqual(
                mission_fields.get(name),
                expected_type,
                f"Field {name} should serialize as {expected_type}",
            )

        controller = {"classname": "target_actor", "count": 42}
        path_target = {"classname": "target_actor", "count": 17}
        actor = {"classname": "monster_actor", "count": 4}
        actor.update(
            {
                "mission_controller": controller,
                "mission_last_controller": controller,
                "mission_prev_path": path_target,
                "mission_path_target": path_target,
                "mission_script_target": None,
                "mission_controller_serial": controller["count"],
                "mission_path_toggle": 1,
                "mission_controller_distance": 256.0,
                "mission_controller_resume": 3.5,
                "mission_path_wait": 2.5,
                "mission_path_time": 12.0,
                "mission_path_speed": 220.0,
                "mission_path_step_speed": 120.0,
                "mission_path_remaining": 640.0,
                "mission_path_state": 2,
                "mission_path_dir": (0.25, -0.5, 0.75),
                "mission_path_velocity": (-24.0, 32.0, 12.0),
                "mission_custom_name": "Escort Alpha",
                "mission_custom_name_time": 5.25,
                "mission_id": "a99",
                "mission_title": "Secure the waypoint",
                "mission_text": "Lead the squad to the next objective.",
                "mission_event": 3,
                "mission_timer_limit": 90,
                "mission_timer_start": 45,
                "mission_flags": 0x6,
                "mission_origin": (1024.0, -32.0, 88.0),
                "mission_angles": (0.0, 270.0, 0.0),
                "mission_velocity": (64.0, 0.0, -4.0),
                "mission_blend": 0.5,
                "mission_radius": 256.0,
            }
        )

        actor["mission_timer_start"] -= 12
        actor["mission_path_toggle"] ^= 1

        entities = [controller, path_target, actor]
        index_map = {id(ent): idx for idx, ent in enumerate(entities)}
        pre_save_snapshot = self._snapshot_fields(actor, mission_fields, index_map)

        serialized_actor = self._serialize_entity(actor, mission_fields, index_map)

        loaded_entities = [
            {"classname": controller["classname"], "count": controller["count"]},
            {"classname": path_target["classname"], "count": path_target["count"]},
            {"classname": actor["classname"], "count": actor["count"]},
        ]
        lookup = {idx: ent for idx, ent in enumerate(loaded_entities)}

        restored_fields = self._restore_entity(serialized_actor, mission_fields, lookup)
        loaded_entities[2].update(restored_fields)

        loaded_index_map = {id(ent): idx for idx, ent in enumerate(loaded_entities)}
        post_load_snapshot = self._snapshot_fields(loaded_entities[2], mission_fields, loaded_index_map)

        self.assertEqual(
            pre_save_snapshot,
            post_load_snapshot,
            "Mission-enabled actor fields should survive a save/load round-trip",
        )


if __name__ == "__main__":
    unittest.main()
