#!/usr/bin/env python3
"""Extract spawn manifest information from Oblivion HLIL listing and compare with repo sources."""

from __future__ import annotations

import argparse
import ast
import json
import math
import re
import struct
import sys
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple

# ----------------------------- utility helpers -----------------------------

def _parse_hex_bytes(line: str) -> List[int]:
    """Return a list of byte values parsed from a line of hex dump text."""
    hex_bytes = re.findall(r"(?i)\b[0-9a-f]{2}\b", line)
    return [int(h, 16) for h in hex_bytes]


def _bytes_to_int_le(bytes_seq: Sequence[int]) -> int:
    if len(bytes_seq) != 4:
        raise ValueError(f"expected 4 bytes, got {len(bytes_seq)}")
    return int.from_bytes(bytes(bytes_seq), byteorder="little", signed=False)


def _decode_float(value: int) -> float:
    return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]


@dataclass
class FieldInfo:
    name: str
    offset: int
    type_id: int
    flags: int


@dataclass
class HLILSpawnInfo:
    classname: str
    function: str
    defaults: Dict[str, List[Dict[str, object]]] = field(default_factory=dict)
    spawnflags: Dict[str, List[int]] = field(default_factory=lambda: {
        "checks": [],
        "sets": [],
        "clears": [],
        "assignments": [],
    })


@dataclass
class RepoSpawnInfo:
    classname: str
    function: str
    defaults: Dict[str, float] = field(default_factory=dict)
    spawnflags: Dict[str, List[int]] = field(default_factory=lambda: {
        "checks": [],
        "sets": [],
        "clears": [],
        "assignments": [],
    })


@dataclass
class SourceFile:
    path: Path
    lines: List[str]
    is_split: bool


@dataclass
class BinarySection:
    name: str
    virtual_address: int
    virtual_size: int
    raw_address: int
    raw_size: int


# ----------------------------- HLIL parsing -----------------------------

class HLILParser:
    def __init__(self, path: Path):
        self.path = path
        self._split_root = self.path.parent / "split"
        self._sources: List[SourceFile] = []
        self.lines: List[str] = []
        for source_path in self._iter_source_paths():
            source_lines = source_path.read_text(encoding="utf-8", errors="replace").splitlines()
            is_split = self._is_split_source(source_path)
            self._sources.append(SourceFile(path=source_path, lines=source_lines, is_split=is_split))
            self.lines.extend(source_lines)

        self._function_blocks: Optional[Dict[str, List[str]]] = None
        self._fields: Optional[Dict[int, FieldInfo]] = None
        self._spawn_map: Optional[Dict[str, str]] = None
        self._string_literals: Optional[Dict[str, str]] = None
        self._interpreted_strings: Optional[List[Dict[str, str]]] = None
        self._binary_path = self._resolve_binary_path()
        self._binary_data: Optional[bytes] = None
        self._binary_sections: Optional[List[BinarySection]] = None
        self._image_base: Optional[int] = None
        self._spawn_table_cache: Dict[Tuple[int, int], Dict[str, str]] = {}
        self._itemlist_cache: Optional[Dict[str, Tuple[int, ...]]] = None
        self._call_graph_entries: Optional[Dict[str, str]] = None

    # -- general helpers --
    def _resolve_binary_path(self) -> Optional[Path]:
        name = self.path.name
        suffix = "_hlil.txt"
        binary_name = name[:-len(suffix)] if name.endswith(suffix) else name
        candidate = self.path.with_name(binary_name)
        if candidate.exists():
            return candidate
        return None

    def _iter_source_paths(self) -> Iterable[Path]:
        yield self.path
        if self._split_root.is_dir():
            for extra_path in sorted(self._split_root.rglob("*.txt")):
                yield extra_path

    def _is_split_source(self, source_path: Path) -> bool:
        try:
            source_path.relative_to(self._split_root)
        except ValueError:
            return False
        return True

    @property
    def function_blocks(self) -> Dict[str, List[str]]:
        if self._function_blocks is None:
            func_pattern = re.compile(
                r"^(?:\d+:)?\s*100[0-9a-f]+\s+(?P<prefix>[^\s].*?)\b(sub_[0-9a-f]+)\("
            )
            blocks: Dict[str, List[str]] = {}

            def append_block(name: str, new_lines: List[str]) -> None:
                existing = blocks.setdefault(name, [])
                for entry in new_lines:
                    if entry not in existing:
                        existing.append(entry)

            for source in self._sources:
                current_name: Optional[str] = None
                current_lines: List[str] = []
                for raw_line in source.lines:
                    line = raw_line.strip()
                    match = func_pattern.match(raw_line)
                    if match:
                        prefix = match.group("prefix")
                        if "return" in prefix or "=" in prefix:
                            if current_name is not None:
                                current_lines.append(line)
                            continue
                        if current_name is not None:
                            append_block(current_name, current_lines)
                        current_name = match.group(2)
                        current_lines = [line]
                    elif current_name is not None:
                        current_lines.append(line)
                if current_name is not None:
                    append_block(current_name, current_lines)
            self._function_blocks = blocks
        return self._function_blocks

    @property
    def fields(self) -> Dict[int, FieldInfo]:
        if self._fields is None:
            entries: Dict[int, FieldInfo] = {}
            ptr_pattern = re.compile(
                r"^(?:\d+:)?\s*100[0-9a-f]+\s+char \(\* (?P<label>data_[0-9a-f]+)\)\[[^]]+\] = (?P<target>data_[0-9a-f]+) {\"(?P<name>[^\"]+)\"}"
            )
            for source in self._sources:
                for idx, raw_line in enumerate(source.lines):
                    m = ptr_pattern.match(raw_line)
                    if not m:
                        continue
                    next_line = ""
                    for j in range(idx + 1, len(source.lines)):
                        candidate = source.lines[j]
                        if candidate.strip():
                            next_line = candidate
                            break
                    if not next_line:
                        continue
                    if not re.search(r"[0-9a-f]{2}\s+[0-9a-f]{2}\s+[0-9a-f]{2}", next_line, re.IGNORECASE):
                        continue
                    byte_values = _parse_hex_bytes(next_line)[:12]
                    if len(byte_values) < 12:
                        continue
                    offset = _bytes_to_int_le(byte_values[0:4])
                    if offset in entries:
                        continue
                    type_id = _bytes_to_int_le(byte_values[4:8])
                    flags = _bytes_to_int_le(byte_values[8:12])
                    entries[offset] = FieldInfo(name=m.group("name"), offset=offset, type_id=type_id, flags=flags)
            self._fields = entries
        return self._fields

    @property
    def spawn_map(self) -> Dict[str, str]:
        if self._spawn_map is None:
            spawn_entries: Dict[str, str] = {}
            literal_map = self._string_literal_map()
            ptr_pattern = re.compile(
                r"^(?:\d+:)?\s*100[0-9a-f]+\s+char \(\* (?P<label>data_[0-9a-f]+)\)\[[^]]+\] = (?P<target>data_[0-9a-f]+) {\"(?P<name>[^\"]+)\"}"
            )
            func_pattern = re.compile(
                r"^(?:\d+:)?\s*100[0-9a-f]+\s+void\* (?P<label>data_[0-9a-f]+) = (?P<func>sub_[0-9a-f]+)"
            )
            sub_decl_pattern = re.compile(r"\b(sub_[0-9a-f]+)\s*\(", re.IGNORECASE)
            for source in self._sources:
                for idx, raw_line in enumerate(source.lines):
                    m = ptr_pattern.match(raw_line)
                    if not m:
                        continue
                    next_line = ""
                    for j in range(idx + 1, len(source.lines)):
                        candidate = source.lines[j]
                        if candidate.strip():
                            next_line = candidate
                            break
                    if not next_line:
                        continue
                    classname = m.group("name")
                    if classname in spawn_entries:
                        continue
                    m_func = func_pattern.match(next_line)
                    if m_func:
                        spawn_entries[classname] = m_func.group("func")
                        continue
                    if source.is_split:
                        follow_func = self._find_next_function_decl(
                            source.lines, idx + 1, sub_decl_pattern
                        )
                        if follow_func:
                            spawn_entries[classname] = follow_func
            for block in self.function_blocks.values():
                table_entries = self._extract_spawn_map_from_spawn_tables(block, literal_map)
                for classname, func in table_entries.items():
                    if classname not in spawn_entries:
                        spawn_entries[classname] = func
                for classname, func in self._extract_spawn_map_from_strcmp(block).items():
                    if classname not in spawn_entries:
                        spawn_entries[classname] = func

            for classname, func in self._spawn_entries_from_binary_tables().items():
                if classname not in spawn_entries:
                    spawn_entries[classname] = func
            item_entries = self._itemlist_entries()
            for classname in ("ammo_mines",):
                if classname in item_entries and classname not in spawn_entries:
                    spawn_entries[classname] = "SpawnItemFromItemlist"

            interpreted_weapons = self._interpreted_classnames(
                category="weapon_descriptor", prefix="weapon_"
            )
            for classname in sorted(interpreted_weapons):
                if classname in item_entries and classname not in spawn_entries:
                    spawn_entries[classname] = "SpawnItemFromItemlist"

            for classname, func in self._call_graph_spawn_entries().items():
                if classname not in spawn_entries:
                    spawn_entries[classname] = func

            self._spawn_map = spawn_entries
        return self._spawn_map

    def _string_literal_map(self) -> Dict[str, str]:
        if self._string_literals is None:
            literal_map: Dict[str, str] = {}
            ptr_pattern = re.compile(
                r"^(?:\d+:)?\s*100[0-9a-f]+\s+char \(\* (?P<label>data_[0-9a-f]+)\)\[[^]]+\] = (?P<target>data_[0-9a-f]+) {\"(?P<name>[^\"]+)\"}"
            )
            for source in self._sources:
                for raw_line in source.lines:
                    match = ptr_pattern.match(raw_line)
                    if not match:
                        continue
                    name = match.group("name")
                    for key in (match.group("label"), match.group("target")):
                        normalized = key.lower()
                        literal_map[normalized] = name
                        if key.startswith("data_"):
                            literal_map[f"0x{key.split('_', 1)[1].lower()}"] = name

            for entry in self._load_interpreted_strings():
                value = entry.get("value")
                if not value:
                    continue
                for key in (entry.get("symbol"), entry.get("address")):
                    if not key:
                        continue
                    normalized = key.lower()
                    literal_map[normalized] = value

            self._string_literals = literal_map
        return self._string_literals

    def _load_interpreted_strings(self) -> List[Dict[str, str]]:
        if self._interpreted_strings is not None:
            return self._interpreted_strings
        interpreted_path = self.path.parent / "interpreted" / "strings.json"
        if interpreted_path.is_file():
            try:
                self._interpreted_strings = json.loads(
                    interpreted_path.read_text(encoding="utf-8")
                )
            except json.JSONDecodeError:
                self._interpreted_strings = []
        else:
            self._interpreted_strings = []
        return self._interpreted_strings

    def _interpreted_classnames(
        self, category: Optional[str] = None, prefix: Optional[str] = None
    ) -> Set[str]:
        classnames: Set[str] = set()
        for entry in self._load_interpreted_strings():
            if category and entry.get("category") != category:
                continue
            value = entry.get("value")
            if not value:
                continue
            normalized = self._normalize_classname(value)
            if prefix and not normalized.startswith(prefix):
                continue
            classnames.add(normalized)
        return classnames

    def _normalize_classname(self, classname: str) -> str:
        return classname.strip().strip("\0")

    def _call_graph_spawn_entries(self) -> Dict[str, str]:
        if self._call_graph_entries is not None:
            return self._call_graph_entries

        targets = ("sub_1001ad80", "sub_100166e7")
        literal_pattern = re.compile(r'"([a-z0-9_]+)"')
        entries: Dict[str, str] = {}

        for func_name, block in self.function_blocks.items():
            call_index = self._locate_call_graph_start(block, targets)
            if call_index is None:
                continue
            for line in block[call_index:]:
                for literal in literal_pattern.findall(line):
                    normalized = self._normalize_classname(literal)
                    if not self._looks_like_classname(normalized):
                        continue
                    entries.setdefault(normalized, func_name)

        self._call_graph_entries = entries
        return entries

    def _locate_call_graph_start(
        self, block: List[str], targets: Sequence[str]
    ) -> Optional[int]:
        if not block:
            return None
        for idx, line in enumerate(block):
            for target in targets:
                if target in line:
                    return idx
        return None

    def _looks_like_classname(self, literal: str) -> bool:
        if not literal or "_" not in literal:
            return False
        prefixes = (
            "target_",
            "trigger_",
            "func_",
            "misc_",
            "monster_",
            "path_",
            "info_",
            "weapon_",
            "item_",
            "ammo_",
            "key_",
            "turret_",
            "point_",
            "bodyque_",
            "light_",
            "script_",
            "model_",
        )
        literal_lower = literal.lower()
        return any(literal_lower.startswith(prefix) for prefix in prefixes)

    def _load_binary_image(self) -> bool:
        if self._binary_path is None:
            return False
        if self._binary_data is not None and self._binary_sections is not None:
            return True
        data = self._binary_path.read_bytes()
        if len(data) < 0x100 or data[:2] != b"MZ":
            return False
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if e_lfanew + 6 >= len(data) or data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
            return False
        number_of_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
        optional_header_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
        optional_header_offset = e_lfanew + 24
        if optional_header_offset + optional_header_size > len(data):
            return False
        image_base = struct.unpack_from("<I", data, optional_header_offset + 28)[0]
        section_offset = optional_header_offset + optional_header_size
        sections: List[BinarySection] = []
        for idx in range(number_of_sections):
            entry_offset = section_offset + idx * 40
            if entry_offset + 40 > len(data):
                break
            name_bytes = data[entry_offset : entry_offset + 8]
            name = name_bytes.split(b"\0", 1)[0].decode("ascii", errors="ignore")
            virtual_size = struct.unpack_from("<I", data, entry_offset + 8)[0]
            virtual_address = struct.unpack_from("<I", data, entry_offset + 12)[0]
            raw_size = struct.unpack_from("<I", data, entry_offset + 16)[0]
            raw_address = struct.unpack_from("<I", data, entry_offset + 20)[0]
            sections.append(
                BinarySection(
                    name=name,
                    virtual_address=virtual_address,
                    virtual_size=virtual_size,
                    raw_address=raw_address,
                    raw_size=raw_size,
                )
            )
        if not sections:
            return False
        self._binary_data = data
        self._binary_sections = sections
        self._image_base = image_base
        return True

    def _va_to_file_offset(self, address: int) -> Optional[int]:
        if not self._load_binary_image() or self._binary_data is None:
            return None
        assert self._binary_sections is not None
        assert self._image_base is not None
        rva = address - self._image_base
        for section in self._binary_sections:
            max_size = section.virtual_size or section.raw_size
            if max_size == 0:
                continue
            start = section.virtual_address
            end = start + max_size
            if start <= rva < end:
                delta = rva - start
                if delta >= section.raw_size:
                    return None
                return section.raw_address + delta
        return None

    def _read_c_string(self, address: int) -> Optional[str]:
        if not self._load_binary_image() or self._binary_data is None:
            return None
        offset = self._va_to_file_offset(address)
        if offset is None or offset >= len(self._binary_data):
            return None
        data = self._binary_data
        end = data.find(b"\0", offset)
        if end == -1:
            return None
        try:
            return data[offset:end].decode("ascii")
        except UnicodeDecodeError:
            return None

    def _is_valid_function_address(self, address: int) -> bool:
        if not self._load_binary_image() or self._binary_sections is None:
            return False
        assert self._image_base is not None
        for section in self._binary_sections:
            if section.name.strip().lower() != ".text":
                continue
            start = self._image_base + section.virtual_address
            end = start + (section.virtual_size or section.raw_size)
            if start <= address < end:
                return True
        return False

    def _parse_spawn_table_from_address(
        self,
        address: int,
        *,
        entry_size: Optional[int] = None,
        literal_map: Optional[Dict[str, str]] = None,
    ) -> Dict[str, str]:
        if entry_size is None:
            if address in (0x10046928, 0x1004A5C0):
                entry_size = 0x48
            else:
                entry_size = 8
        cache_key = (address, entry_size)
        if cache_key in self._spawn_table_cache:
            return self._spawn_table_cache[cache_key]
        entries: Dict[str, str] = {}
        if not self._load_binary_image() or self._binary_data is None:
            return entries
        offset = self._va_to_file_offset(address)
        if offset is None:
            return entries
        data = self._binary_data
        literal_map = literal_map or self._string_literal_map()
        seen_valid = 0
        invalid_streak = 0
        while offset + entry_size <= len(data):
            name_ptr, func_ptr = struct.unpack_from("<II", data, offset)
            classname = self._resolve_classname_from_pointer(name_ptr, literal_map)
            if not classname:
                classname = self._read_c_string(name_ptr)
            if not classname or not self._is_valid_function_address(func_ptr):
                if seen_valid:
                    invalid_streak += 1
                    if invalid_streak >= 64:
                        break
                offset += entry_size
                continue
            invalid_streak = 0
            seen_valid += 1
            normalized = self._normalize_classname(classname)
            if normalized not in entries:
                entries[normalized] = f"sub_{func_ptr:08x}"
            offset += entry_size
        self._spawn_table_cache[cache_key] = entries
        return entries

    def _resolve_classname_from_pointer(
        self, pointer: int, literal_map: Dict[str, str]
    ) -> Optional[str]:
        for key in (
            f"data_{pointer:08x}",
            f"0x{pointer:08x}",
        ):
            value = literal_map.get(key)
            if value:
                return value
        return None

    def _spawn_entries_from_binary_tables(self) -> Dict[str, str]:
        literal_map = self._string_literal_map()
        combined: Dict[str, str] = {}
        for base in (0x10046928, 0x1004A5C0):
            entries = self._parse_spawn_table_from_address(
                base, entry_size=0x48, literal_map=literal_map
            )
            for classname, func in entries.items():
                if classname not in combined:
                    combined[classname] = func
        return combined

    def _extract_spawn_map_from_spawn_tables(
        self,
        block: List[str],
        literal_map: Optional[Dict[str, str]] = None,
    ) -> Dict[str, str]:
        if not block:
            return {}
        if literal_map is None:
            literal_map = self._string_literal_map()

        results: Dict[str, str] = {}
        block_text = "\n".join(block)
        entry_pattern = re.compile(
            r"\{\s*(?P<raw>(?:\&\s*)?data_[0-9a-f]+|0x[0-9a-f]+|\"[^\"]+\")\s*,\s*(?P<func>sub_[0-9a-f]+)\s*\}",
            re.IGNORECASE,
        )
        for match in entry_pattern.finditer(block_text):
            raw = match.group("raw").strip()
            func = match.group("func")
            classname = self._resolve_classname_from_literal(raw, literal_map)
            if not classname:
                continue
            normalized = self._normalize_classname(classname)
            if normalized not in results:
                results[normalized] = func

        if "spawn function" in block_text.lower() and "data_1004a5c0" in block_text.lower():
            table_entries = self._parse_spawn_table_from_address(0x1004A5C0)
            for classname, func in table_entries.items():
                if classname not in results:
                    results[classname] = func

        if not any("switch (" in line for line in block):
            return results

        goto_pattern = re.compile(r"goto\s+(label_[0-9a-f]+)", re.IGNORECASE)
        return_pattern = re.compile(
            r"return\s+(sub_[0-9a-f]+)(?:\b|(?=\())", re.IGNORECASE
        )
        label_pattern = re.compile(r"(label_[0-9a-f]+):", re.IGNORECASE)
        label_indices: Dict[str, int] = {}
        for idx, line in enumerate(block):
            m_label = label_pattern.search(line)
            if m_label:
                label_indices[m_label.group(1)] = idx

        case_indices: List[int] = []
        case_pattern = re.compile(r"\b(case|default)\b", re.IGNORECASE)
        for idx, line in enumerate(block):
            if case_pattern.search(line):
                case_indices.append(idx)
        if not case_indices:
            return results
        case_indices.append(len(block))

        strcmp_call_pattern = re.compile(
            r"sub_10038b20\([^,]+,\s*\"([^\"]+)\"\)", re.IGNORECASE
        )
        for pos, start in enumerate(case_indices[:-1]):
            end = case_indices[pos + 1]
            for idx in range(start, end):
                line = block[idx]
                for literal_match in strcmp_call_pattern.finditer(line):
                    classname = self._normalize_classname(literal_match.group(1))
                    if classname in results:
                        continue
                    target = self._resolve_strcmp_chain(
                        block,
                        idx + 1,
                        end,
                        goto_pattern,
                        return_pattern,
                        label_indices,
                    )
                    if target:
                        results[classname] = target
        return results

    def _resolve_classname_from_literal(
        self, raw: str, literal_map: Dict[str, str]
    ) -> Optional[str]:
        token = raw.strip()
        if token.startswith("\"") and token.endswith("\""):
            return token.strip("\"")
        if token.startswith("&"):
            token = token[1:].strip()
        lookup = literal_map.get(token.lower())
        if lookup:
            return lookup
        if token.lower().startswith("0x"):
            try:
                as_int = int(token, 16)
            except ValueError:
                return None
            lookup = literal_map.get(f"data_{as_int:08x}")
            if lookup:
                return lookup
        return None

    def _find_next_function_decl(
        self,
        lines: Sequence[str],
        start_index: int,
        sub_decl_pattern: re.Pattern[str],
    ) -> Optional[str]:
        for idx in range(start_index, len(lines)):
            candidate = lines[idx]
            stripped = candidate.strip()
            if not stripped:
                continue
            if stripped.startswith(("#", "//", "/*", "*", "*/")):
                continue
            match = sub_decl_pattern.search(candidate)
            if match:
                return match.group(1)
        return None

    def _extract_spawn_map_from_strcmp(self, block: List[str]) -> Dict[str, str]:
        literal_pattern = re.compile(
            r'(?:const\s+)?char(?:\s+const)?\s*\*\s+[^=]+\s*=\s*"([^"]+)"'
        )
        goto_pattern = re.compile(r'goto\s+(label_[0-9a-f]+)')
        return_pattern = re.compile(r'return\s+(sub_[0-9a-f]+)(?:\b|(?=\())')
        label_pattern = re.compile(r'(label_[0-9a-f]+):')

        label_indices: Dict[str, int] = {}
        for idx, line in enumerate(block):
            m_label = label_pattern.search(line)
            if m_label:
                label_indices[m_label.group(1)] = idx

        literal_positions: List[Tuple[int, str]] = []
        for idx, line in enumerate(block):
            m_literal = literal_pattern.search(line)
            if m_literal:
                literal_positions.append((idx, m_literal.group(1)))

        results: Dict[str, str] = {}
        for pos, (line_idx, classname) in enumerate(literal_positions):
            search_limit = (
                literal_positions[pos + 1][0]
                if pos + 1 < len(literal_positions)
                else len(block)
            )
            classname = self._normalize_classname(classname)
            target = self._resolve_strcmp_chain(
                block,
                line_idx + 1,
                search_limit,
                goto_pattern,
                return_pattern,
                label_indices,
            )
            if target:
                results[classname] = target
        return results

    def _resolve_strcmp_chain(
        self,
        block: List[str],
        start_index: int,
        search_limit: int,
        goto_pattern: re.Pattern[str],
        return_pattern: re.Pattern[str],
        label_indices: Dict[str, int],
    ) -> Optional[str]:
        if not block:
            return None

        queue: deque[int] = deque()
        queue.append(max(0, start_index))
        visited_starts: Set[int] = set()

        while queue:
            current = queue.popleft()
            if current in visited_starts or current >= len(block):
                continue
            visited_starts.add(current)

            end = search_limit if current == start_index else len(block)
            idx = current
            while idx < end and idx < len(block):
                line = block[idx].strip()
                if line:
                    m_return = return_pattern.search(line)
                    if m_return:
                        return m_return.group(1)

                    for m_goto in goto_pattern.finditer(line):
                        label_name = m_goto.group(1)
                        target_idx = label_indices.get(label_name)
                        if target_idx is not None and target_idx not in visited_starts:
                            queue.append(target_idx)
                idx += 1

        return None

    # -- extraction --
    def build_manifest(self) -> Dict[str, HLILSpawnInfo]:
        manifest: Dict[str, HLILSpawnInfo] = {}
        fields = self.fields
        func_blocks = self.function_blocks
        for classname, func in sorted(self.spawn_map.items()):
            info = HLILSpawnInfo(classname=classname, function=func)
            block = func_blocks.get(func)
            if block:
                info.defaults = self._extract_defaults(block, fields)
                info.spawnflags = self._extract_spawnflags(block)
            if not info.defaults and func == "SpawnItemFromItemlist":
                item_defaults = self._itemlist_defaults_for(classname)
                if item_defaults:
                    info.defaults = item_defaults
            manifest[classname] = info
        return manifest

    def _itemlist_entries(self) -> Dict[str, Tuple[int, ...]]:
        if self._itemlist_cache is not None:
            return self._itemlist_cache
        entries: Dict[str, Tuple[int, ...]] = {}
        if not self._load_binary_image() or self._binary_data is None:
            self._itemlist_cache = entries
            return entries
        base_address = 0x10046928
        entry_size = 0x48
        offset = self._va_to_file_offset(base_address)
        if offset is None:
            self._itemlist_cache = entries
            return entries
        data = self._binary_data
        idx = 0
        while offset + (idx + 1) * entry_size <= len(data):
            start = offset + idx * entry_size
            chunk = data[start : start + entry_size]
            if len(chunk) < entry_size:
                break
            values = struct.unpack("<" + "I" * (entry_size // 4), chunk)
            if not any(values):
                if idx != 0:
                    break
                idx += 1
                continue
            classname_ptr = values[0]
            classname = self._read_c_string(classname_ptr)
            if classname:
                entries[self._normalize_classname(classname)] = values
            idx += 1
        self._itemlist_cache = entries
        return entries

    def _itemlist_defaults_for(self, classname: str) -> Dict[str, List[Dict[str, object]]]:
        entries = self._itemlist_entries()
        values = entries.get(classname)
        if not values:
            return {}
        defaults: Dict[str, List[Dict[str, object]]] = {}
        for idx, raw_value in enumerate(values):
            offset = idx * 4
            field_name = f"offset_0x{offset:x}"
            defaults[field_name] = [{"offset": offset, "value": int(raw_value)}]
        return defaults

    def _extract_defaults(self, block: List[str], fields: Dict[int, FieldInfo]) -> Dict[str, List[Dict[str, object]]]:
        results: Dict[str, List[Dict[str, object]]] = {}
        assign_pattern = re.compile(
            r"\*\((?:[a-z0-9_]+ \+ )?0x([0-9a-f]+)\) = (0x[0-9a-f]+|-?\d+)",
            re.IGNORECASE,
        )
        for line in block:
            for match in assign_pattern.finditer(line):
                offset = int(match.group(1), 16)
                raw_val = match.group(2)
                if raw_val.lower().startswith("0x"):
                    value = int(raw_val, 16)
                else:
                    value = int(raw_val, 10)
                field_info = fields.get(offset)
                if not field_info or offset < 0x100:
                    field_name = f"offset_0x{offset:x}"
                else:
                    field_name = field_info.name
                    if field_info.type_id == 1:  # float-like
                        value = _decode_float(value)
                    elif field_info.type_id in (0x10001, 0x10002):  # handle extended markers
                        value = value
                entry = {"value": value, "offset": offset}
                results.setdefault(field_name, []).append(entry)
        return results

    def _extract_spawnflags(self, block: List[str]) -> Dict[str, List[int]]:
        checks: Set[int] = set()
        sets: Set[int] = set()
        clears: Set[int] = set()
        assignments: Set[int] = set()
        alias_names: Set[str] = set()
        alias_pattern = re.compile(
            r"(?P<lhs>[A-Za-z_][\w\.:]*)\s*=\s*(?:\(\*[^)]*0x11c[^)]*\)(?:\.\w+)?|\*[^;]*0x11c[^;]*)",
            re.IGNORECASE,
        )
        direct_check_pattern = re.compile(
            r"\([^)]*0x11c[^)]*\)\s*&\s*(0x[0-9a-f]+|\d+)",
            re.IGNORECASE,
        )
        direct_assign_pattern = re.compile(
            r"\*\([^)]*0x11c[^)]*\)\s*=\s*(0x[0-9a-f]+|\d+)",
            re.IGNORECASE,
        )
        direct_clear_pattern = re.compile(
            r"\*\([^)]*0x11c[^)]*\)\s*=\s*\*\([^)]*0x11c[^)]*\)\s*&\s*(0x[0-9a-f]+|\d+)",
            re.IGNORECASE,
        )
        direct_or_pattern = re.compile(r"\|=\s*(0x[0-9a-f]+|\d+)", re.IGNORECASE)
        direct_and_pattern = re.compile(r"&=\s*(0x[0-9a-f]+|\d+)", re.IGNORECASE)
        alias_op_pattern = re.compile(
            r"(?P<alias>[A-Za-z_][\w\.:]*)\s*(?P<op>\|=|&=)\s*(0x[0-9a-f]+|\d+)",
            re.IGNORECASE,
        )
        alias_check_pattern = re.compile(
            r"([A-Za-z_][\w\.:]*)\s*&\s*(0x[0-9a-f]+|\d+)",
            re.IGNORECASE,
        )

        for line in block:
            if "0x11c" not in line:
                continue
            for alias_match in alias_pattern.finditer(line):
                alias_names.add(alias_match.group("lhs"))
            m_assign = direct_assign_pattern.search(line)
            if m_assign:
                assignments.add(int(m_assign.group(1), 0))
            for m in direct_or_pattern.finditer(line):
                sets.add(int(m.group(1), 0))
            for m in direct_and_pattern.finditer(line):
                mask = int(m.group(1), 0) & 0xFFFFFFFF
                cleared = (~mask) & 0xFFFFFFFF
                if 0 < cleared < 0xFFFFFFFF:
                    clears.add(cleared)
            m_clear = direct_clear_pattern.search(line)
            if m_clear:
                mask = int(m_clear.group(1), 0) & 0xFFFFFFFF
                cleared = (~mask) & 0xFFFFFFFF
                if 0 < cleared < 0xFFFFFFFF:
                    clears.add(cleared)
            for m in direct_check_pattern.finditer(line):
                checks.add(int(m.group(1), 0))

        for line in block:
            for m in alias_op_pattern.finditer(line):
                alias = m.group("alias")
                value = int(m.group(3), 0)
                if alias not in alias_names:
                    continue
                if m.group("op") == "|=":
                    sets.add(value)
                else:
                    mask = value & 0xFFFFFFFF
                    cleared = (~mask) & 0xFFFFFFFF
                    if 0 < cleared < 0xFFFFFFFF:
                        clears.add(cleared)
            for m in alias_check_pattern.finditer(line):
                alias = m.group(1)
                if alias not in alias_names:
                    continue
                checks.add(int(m.group(2), 0))

        return {
            "checks": sorted(checks),
            "sets": sorted(sets),
            "clears": sorted(clears),
            "assignments": sorted(assignments),
        }


# ----------------------------- Repo parsing -----------------------------

class MacroResolver:
    def __init__(self, source_paths: Iterable[Path]):
        self.definitions: Dict[str, str] = {}
        pattern = re.compile(r"^\s*#\s*define\s+(\w+)\s+(.+)$")
        for path in source_paths:
            for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
                match = pattern.match(line)
                if match:
                    name, expr = match.groups()
                    if name not in self.definitions:
                        self.definitions[name] = expr.strip()
        self._cache: Dict[str, int] = {}

    def evaluate(self, name: str) -> Optional[int]:
        if name in self._cache:
            return self._cache[name]
        expr = self.definitions.get(name)
        if expr is None:
            return None
        try:
            value = self._eval_expr(expr)
        except Exception:
            return None
        self._cache[name] = value
        return value

    def _eval_expr(self, expr: str) -> int:
        tree = ast.parse(expr, mode="eval")

        def _eval(node: ast.AST) -> int:
            if isinstance(node, ast.Expression):
                return _eval(node.body)
            if isinstance(node, ast.Num):  # type: ignore[attr-defined]
                return int(node.n)
            if isinstance(node, ast.Constant):
                if isinstance(node.value, (int, float)):
                    return int(node.value)
                if isinstance(node.value, str) and node.value.startswith("0x"):
                    return int(node.value, 16)
                raise ValueError
            if isinstance(node, ast.UnaryOp):
                operand = _eval(node.operand)
                if isinstance(node.op, ast.USub):
                    return -operand
                if isinstance(node.op, ast.UAdd):
                    return operand
                if isinstance(node.op, ast.Invert):
                    return (~operand) & 0xFFFFFFFF
                raise ValueError
            if isinstance(node, ast.BinOp):
                left = _eval(node.left)
                right = _eval(node.right)
                if isinstance(node.op, ast.Add):
                    return left + right
                if isinstance(node.op, ast.Sub):
                    return left - right
                if isinstance(node.op, ast.Mult):
                    return left * right
                if isinstance(node.op, ast.FloorDiv) or isinstance(node.op, ast.Div):
                    return int(left / right)
                if isinstance(node.op, ast.BitOr):
                    return left | right
                if isinstance(node.op, ast.BitAnd):
                    return left & right
                if isinstance(node.op, ast.BitXor):
                    return left ^ right
                if isinstance(node.op, ast.LShift):
                    return left << right
                if isinstance(node.op, ast.RShift):
                    return left >> right
                raise ValueError
            if isinstance(node, ast.Name):
                resolved = self.evaluate(node.id)
                if resolved is None:
                    raise ValueError
                return resolved
            raise ValueError

        return _eval(tree)


class RepoParser:
    def __init__(self, root: Path):
        self.root = root
        self.game_dir = self.root / "src" / "game"
        self.source_files = list(self.game_dir.glob("**/*.c")) + list(self.game_dir.glob("**/*.h"))
        self.macro_resolver = MacroResolver(self.source_files)
        self.spawn_map = self._parse_spawn_map()
        self.functions = self._parse_functions()

    def _parse_spawn_map(self) -> Dict[str, str]:
        spawn_map: Dict[str, str] = {}
        spawn_file = self.game_dir / "g_spawn.c"
        text = spawn_file.read_text(encoding="utf-8", errors="ignore")
        array_pattern = re.compile(r"\{\s*\"([^\"]+)\",\s*(SP_[^}]+)\}")
        for classname, func in array_pattern.findall(text):
            spawn_map[classname] = func.strip()
        for classname in sorted(self._parse_itemlist_classnames()):
            spawn_map.setdefault(classname, "SpawnItemFromItemlist")
        rotate_flag = self.macro_resolver.evaluate("OBLIVION_ENABLE_ROTATE_TRAIN")
        if rotate_flag is not None and rotate_flag == 0:
            spawn_map.pop("func_rotate_train", None)
        return spawn_map

    def _parse_itemlist_classnames(self) -> Set[str]:
        items_file = self.game_dir / "g_items.c"
        text = items_file.read_text(encoding="utf-8", errors="ignore")
        anchor = re.search(r"gitem_t\s+itemlist\s*\[\]\s*=", text)
        if not anchor:
            return set()
        brace_start = text.find("{", anchor.end())
        if brace_start == -1:
            return set()
        depth = 0
        brace_end = None
        for idx in range(brace_start, len(text)):
            ch = text[idx]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    brace_end = idx
                    break
        if brace_end is None:
            return set()
        block = text[brace_start:brace_end]
        classnames = set(re.findall(r"\{\s*\"([^\"]+)\"\s*,", block))
        return classnames

    def _parse_functions(self) -> Dict[str, List[str]]:
        functions: Dict[str, List[str]] = {}
        func_pattern = re.compile(r"^\w[\w\s\*]*\b(SP_[a-zA-Z0-9_]+)\s*\(([^)]*)\)")
        brace_stack: List[str] = []
        current_name: Optional[str] = None
        current_lines: List[str] = []
        for path in self.source_files:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
            for line in lines:
                if current_name is None:
                    match = func_pattern.match(line)
                    if match:
                        current_name = match.group(1)
                        brace_stack = []
                        if "{" in line:
                            brace_stack.append("{")
                        current_lines = [line]
                    continue
                else:
                    current_lines.append(line)
                    brace_stack.extend(ch for ch in line if ch == "{")
                    for ch in line:
                        if ch == "}":
                            if brace_stack:
                                brace_stack.pop()
                            else:
                                brace_stack = []
                    if not brace_stack and line.strip().endswith("}"):
                        functions[current_name] = current_lines[:]
                        current_name = None
                        current_lines = []
        return functions

    def build_manifest(self) -> Dict[str, RepoSpawnInfo]:
        manifest: Dict[str, RepoSpawnInfo] = {}
        for classname, func in sorted(self.spawn_map.items()):
            info = RepoSpawnInfo(classname=classname, function=func)
            lines = self.functions.get(func)
            if lines:
                info.defaults = self._extract_defaults(lines)
                info.spawnflags = self._extract_spawnflags(lines)
            manifest[classname] = info
        return manifest

    def _extract_defaults(self, lines: List[str]) -> Dict[str, float]:
        defaults: Dict[str, float] = {}
        assign_pattern = re.compile(
            r"\b([a-zA-Z_][a-zA-Z0-9_]*)->([a-zA-Z0-9_\.]+)\s*=\s*([^;]+)"
        )
        for line in lines:
            for match in assign_pattern.finditer(line):
                field = match.group(2)
                expr = match.group(3).strip()
                value = self._evaluate_default_expr(expr)
                if value is None:
                    continue
                defaults[field] = value
        return defaults

    def _evaluate_default_expr(self, expr: str) -> Optional[float]:
        expr = expr.strip()
        if not expr:
            return None
        normalized = self._normalize_c_numeric_expr(expr)
        value = self._eval_ast_numeric_expr(normalized)
        if value is not None:
            return value
        return self._parse_literal_or_macro(normalized)

    def _normalize_c_numeric_expr(self, expr: str) -> str:
        expr = expr.rstrip(";").strip()
        cast_pattern = re.compile(r"^\(\s*(?:const\s+)?(?:struct\s+)?[a-zA-Z_][\w\s\*]*\)")
        while True:
            match = cast_pattern.match(expr)
            if not match:
                break
            expr = expr[match.end() :].lstrip()
        expr = re.sub(r"(\d+\.\d+)[fF]\b", r"\\1", expr)
        expr = re.sub(r"(?<![0-9a-fA-FxX])(\d+)[fF]\b", r"\\1", expr)
        return expr

    def _eval_ast_numeric_expr(self, expr: str) -> Optional[float]:
        try:
            tree = ast.parse(expr, mode="eval")
        except SyntaxError:
            return None

        def _eval(node: ast.AST) -> Optional[float]:
            if isinstance(node, ast.Expression):
                return _eval(node.body)
            if isinstance(node, ast.Constant):
                if isinstance(node.value, (int, float)):
                    return float(node.value)
                if isinstance(node.value, str) and node.value.lower().startswith("0x"):
                    try:
                        return float(int(node.value, 16))
                    except ValueError:
                        return None
                return None
            if hasattr(ast, "Num") and isinstance(node, ast.Num):  # type: ignore[attr-defined]
                return float(node.n)
            if isinstance(node, ast.UnaryOp):
                operand = _eval(node.operand)
                if operand is None:
                    return None
                if isinstance(node.op, ast.USub):
                    return -operand
                if isinstance(node.op, ast.UAdd):
                    return operand
                if isinstance(node.op, ast.Invert):
                    return float((~int(operand)) & 0xFFFFFFFF)
                return None
            if isinstance(node, ast.BinOp):
                left = _eval(node.left)
                right = _eval(node.right)
                if left is None or right is None:
                    return None
                if isinstance(node.op, ast.Add):
                    return left + right
                if isinstance(node.op, ast.Sub):
                    return left - right
                if isinstance(node.op, ast.Mult):
                    return left * right
                if isinstance(node.op, ast.Div):
                    return left / right
                if isinstance(node.op, ast.FloorDiv):
                    return float(int(left / right))
                if isinstance(node.op, ast.Mod):
                    return left % right
                if isinstance(node.op, ast.BitOr):
                    return float(int(left) | int(right))
                if isinstance(node.op, ast.BitAnd):
                    return float(int(left) & int(right))
                if isinstance(node.op, ast.BitXor):
                    return float(int(left) ^ int(right))
                if isinstance(node.op, ast.LShift):
                    return float(int(left) << int(right))
                if isinstance(node.op, ast.RShift):
                    return float(int(left) >> int(right))
                return None
            if isinstance(node, ast.Name):
                resolved = self.macro_resolver.evaluate(node.id)
                if resolved is None:
                    return None
                return float(resolved)
            return None

        return _eval(tree)

    def _parse_literal_or_macro(self, expr: str) -> Optional[float]:
        token = expr.strip()
        if not token:
            return None
        if token.lower().startswith("0x"):
            try:
                return float(int(token, 16))
            except ValueError:
                return None
        try:
            return float(token)
        except ValueError:
            resolved = self.macro_resolver.evaluate(token)
            if resolved is not None:
                return float(resolved)
        return None

    def _extract_spawnflags(self, lines: List[str]) -> Dict[str, List[int]]:
        checks: List[int] = []
        sets: List[int] = []
        clears: List[int] = []
        assignments: List[int] = []
        resolver = self.macro_resolver

        def resolve_token(token: str) -> Optional[int]:
            token = token.strip()
            if not token:
                return None
            if token.lower().startswith("0x"):
                return int(token, 16)
            if token.isdigit():
                return int(token)
            return resolver.evaluate(token)

        for line in lines:
            if "spawnflags" not in line:
                continue
            if "|=" in line:
                token = line.split("|=")[-1].split(";")[0]
                value = resolve_token(token)
                if value is not None and value not in sets:
                    sets.append(value)
            if "&=" in line:
                token = line.split("&=")[-1].split(";")[0]
                value = resolve_token(token)
                if value is not None:
                    mask = value & 0xFFFFFFFF
                    cleared = (~mask) & 0xFFFFFFFF
                    if 0 < cleared < 0xFFFFFFFF and cleared not in clears:
                        clears.append(cleared)
            if "spawnflags =" in line:
                token = line.split("spawnflags =", 1)[1].split(";")[0]
                value = resolve_token(token)
                if value is not None and value not in assignments:
                    assignments.append(value)
            check_pattern = re.compile(r"spawnflags\s*&\s*([^&|)]+)")
            for match in check_pattern.finditer(line):
                value = resolve_token(match.group(1))
                if value is not None and value not in checks:
                    checks.append(value)
        return {
            "checks": sorted([v for v in checks if v is not None]),
            "sets": sorted([v for v in sets if v is not None]),
            "clears": sorted([v for v in clears if v is not None]),
            "assignments": sorted([v for v in assignments if v is not None]),
        }


# ----------------------------- comparison -----------------------------

@dataclass
class ComparisonResult:
    missing_in_repo: List[str] = field(default_factory=list)
    missing_in_hlil: List[str] = field(default_factory=list)
    spawnflag_mismatches: Dict[str, Dict[str, Tuple[List[int], List[int]]]] = field(default_factory=dict)
    default_mismatches: Dict[str, Dict[str, Tuple[List[Dict[str, object]], Optional[float]]]] = field(default_factory=dict)


def compare_manifests(hlil: Dict[str, HLILSpawnInfo], repo: Dict[str, RepoSpawnInfo]) -> ComparisonResult:
    result = ComparisonResult()
    hlil_classnames = set(hlil)
    repo_classnames = set(repo)
    result.missing_in_repo = sorted(hlil_classnames - repo_classnames)
    result.missing_in_hlil = sorted(repo_classnames - hlil_classnames)

    shared = sorted(hlil_classnames & repo_classnames)
    for classname in shared:
        hl = hlil[classname]
        rp = repo[classname]
        spawnflag_diff: Dict[str, Tuple[List[int], List[int]]] = {}
        for key in ("checks", "sets", "clears", "assignments"):
            hl_vals = sorted(set(hl.spawnflags.get(key, [])))
            rp_vals = sorted(set(rp.spawnflags.get(key, [])))
            if hl_vals != rp_vals:
                spawnflag_diff[key] = (hl_vals, rp_vals)
        if spawnflag_diff:
            result.spawnflag_mismatches[classname] = spawnflag_diff

        default_diff: Dict[str, Tuple[List[Dict[str, object]], Optional[float]]] = {}
        for field_name, entries in hl.defaults.items():
            repo_value = rp.defaults.get(field_name)
            if repo_value is None:
                default_diff[field_name] = (entries, None)
            else:
                hl_values = sorted({json.dumps(e, sort_keys=True) for e in entries})
                if not math.isclose(float(json.loads(hl_values[0])["value"]), repo_value, rel_tol=1e-4, abs_tol=1e-4):
                    default_diff[field_name] = (entries, repo_value)
        if default_diff:
            result.default_mismatches[classname] = default_diff

    return result


# ----------------------------- command line interface -----------------------------

def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hlil", type=Path, default=Path("references/HLIL/oblivion/gamex86.dll_hlil.txt"))
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, help="Write combined manifest JSON to this path")
    parser.add_argument("--comparison", type=Path, help="Write comparison JSON to this path")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON when writing to stdout")
    args = parser.parse_args(argv)

    hlil_parser = HLILParser(args.hlil)
    repo_parser = RepoParser(args.repo)

    hlil_manifest = hlil_parser.build_manifest()
    repo_manifest = repo_parser.build_manifest()
    comparison = compare_manifests(hlil_manifest, repo_manifest)

    combined = {
        "hlil": {
            classname: {
                "function": info.function,
                "defaults": info.defaults,
                "spawnflags": info.spawnflags,
            }
            for classname, info in sorted(hlil_manifest.items())
        },
        "repo": {
            classname: {
                "function": info.function,
                "defaults": info.defaults,
                "spawnflags": info.spawnflags,
            }
            for classname, info in sorted(repo_manifest.items())
        },
    }

    if args.output:
        args.output.write_text(json.dumps(combined, indent=2, sort_keys=True))
    if args.comparison:
        args.comparison.write_text(
            json.dumps(
                {
                    "missing_in_repo": comparison.missing_in_repo,
                    "missing_in_hlil": comparison.missing_in_hlil,
                    "spawnflag_mismatches": comparison.spawnflag_mismatches,
                    "default_mismatches": comparison.default_mismatches,
                },
                indent=2,
                sort_keys=True,
            )
        )

    if not args.output:
        dump = {
            "combined": combined,
            "comparison": {
                "missing_in_repo": comparison.missing_in_repo,
                "missing_in_hlil": comparison.missing_in_hlil,
                "spawnflag_mismatches": comparison.spawnflag_mismatches,
                "default_mismatches": comparison.default_mismatches,
            },
        }
        json_kwargs = {"indent": 2, "sort_keys": True} if args.pretty else {}
        json.dump(dump, sys.stdout, **json_kwargs)
        if args.pretty:
            sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
