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


# ----------------------------- HLIL parsing -----------------------------

class HLILParser:
    def __init__(self, path: Path):
        self.path = path
        split_root = self.path.parent / "split"
        source_paths: List[Path] = [self.path]
        if split_root.exists():
            extra_paths = sorted(split_root.glob("**/*.txt"))
            source_paths.extend(extra_paths)

        self._sources: List[Tuple[Path, List[str]]] = []
        self.lines: List[str] = []
        for source_path in source_paths:
            source_lines = source_path.read_text(encoding="utf-8", errors="replace").splitlines()
            self._sources.append((source_path, source_lines))
            self.lines.extend(source_lines)

        self._function_blocks: Optional[Dict[str, List[str]]] = None
        self._fields: Optional[Dict[int, FieldInfo]] = None
        self._spawn_map: Optional[Dict[str, str]] = None

    # -- general helpers --
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

            for _, source_lines in self._sources:
                current_name: Optional[str] = None
                current_lines: List[str] = []
                for raw_line in source_lines:
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
            for _, source_lines in self._sources:
                for idx, raw_line in enumerate(source_lines):
                    m = ptr_pattern.match(raw_line)
                    if not m:
                        continue
                    next_line = ""
                    for j in range(idx + 1, len(source_lines)):
                        candidate = source_lines[j]
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
            ptr_pattern = re.compile(
                r"^(?:\d+:)?\s*100[0-9a-f]+\s+char \(\* (?P<label>data_[0-9a-f]+)\)\[[^]]+\] = (?P<target>data_[0-9a-f]+) {\"(?P<name>[^\"]+)\"}"
            )
            func_pattern = re.compile(
                r"^(?:\d+:)?\s*100[0-9a-f]+\s+void\* (?P<label>data_[0-9a-f]+) = (?P<func>sub_[0-9a-f]+)"
            )
            for _, source_lines in self._sources:
                for idx, raw_line in enumerate(source_lines):
                    m = ptr_pattern.match(raw_line)
                    if not m:
                        continue
                    next_line = ""
                    for j in range(idx + 1, len(source_lines)):
                        candidate = source_lines[j]
                        if candidate.strip():
                            next_line = candidate
                            break
                    if not next_line:
                        continue
                    m_func = func_pattern.match(next_line)
                    if not m_func:
                        continue
                    classname = m.group("name")
                    if classname in spawn_entries:
                        continue
                    func = m_func.group("func")
                    spawn_entries[classname] = func
            for block in self.function_blocks.values():
                for classname, func in self._extract_spawn_map_from_strcmp(block).items():
                    if classname not in spawn_entries:
                        spawn_entries[classname] = func
            self._spawn_map = spawn_entries
        return self._spawn_map

    def _extract_spawn_map_from_strcmp(self, block: List[str]) -> Dict[str, str]:
        literal_pattern = re.compile(r'char\*\s+[^=]+\s*=\s*"([^"]+)"')
        if_pattern = re.compile(r'^100[0-9a-f]+\s+if\s*\([^)]*==\s*0\)')
        goto_pattern = re.compile(r'goto\s+(label_[0-9a-f]+)')
        return_pattern = re.compile(r'return\s+(sub_[0-9a-f]+)\b')
        label_pattern = re.compile(r'(label_[0-9a-f]+):')
        label_indices: Dict[str, int] = {}
        for idx, line in enumerate(block):
            m_label = label_pattern.search(line)
            if m_label:
                label_indices[m_label.group(1)] = idx

        results: Dict[str, str] = {}
        idx = 0
        while idx < len(block):
            line = block[idx]
            m_literal = literal_pattern.search(line)
            if not m_literal:
                idx += 1
                continue
            classname = m_literal.group(1)
            target_func: Optional[str] = None
            search_limit = min(len(block), idx + 80)
            inner = idx + 1
            while inner < search_limit:
                next_line = block[inner]
                if literal_pattern.search(next_line):
                    break
                stripped = next_line.strip()
                m_direct_return = return_pattern.search(stripped)
                if m_direct_return:
                    target_func = m_direct_return.group(1)
                    break
                if if_pattern.match(stripped):
                    candidate = self._resolve_strcmp_if(
                        block,
                        inner,
                        label_indices,
                        goto_pattern,
                        return_pattern,
                        label_pattern,
                    )
                    if candidate:
                        target_func = candidate
                        break
                inner += 1
            if target_func:
                results[classname] = target_func
            idx += 1
        return results

    def _resolve_strcmp_if(
        self,
        block: List[str],
        if_index: int,
        label_indices: Dict[str, int],
        goto_pattern: re.Pattern[str],
        return_pattern: re.Pattern[str],
        label_pattern: re.Pattern[str],
    ) -> Optional[str]:
        for offset in range(1, 12):
            idx = if_index + offset
            if idx >= len(block):
                break
            text = block[idx].strip()
            if not text:
                continue
            m_return = return_pattern.search(text)
            if m_return:
                return m_return.group(1)
            m_goto = goto_pattern.search(text)
            if m_goto:
                return self._resolve_strcmp_label(
                    block,
                    m_goto.group(1),
                    label_indices,
                    goto_pattern,
                    return_pattern,
                    label_pattern,
                )
            if text.startswith("else"):
                break
        return None

    def _resolve_strcmp_label(
        self,
        block: List[str],
        label: str,
        label_indices: Dict[str, int],
        goto_pattern: re.Pattern[str],
        return_pattern: re.Pattern[str],
        label_pattern: re.Pattern[str],
    ) -> Optional[str]:
        if label not in label_indices:
            return None
        visited: Set[str] = set()
        queue: List[str] = [label]
        while queue:
            current = queue.pop()
            if current in visited:
                continue
            visited.add(current)
            start = label_indices.get(current)
            if start is None:
                continue
            end = min(len(block), start + 80)
            for idx in range(start + 1, end):
                text = block[idx].strip()
                if not text:
                    continue
                m_return = return_pattern.search(text)
                if m_return:
                    return m_return.group(1)
                m_goto = goto_pattern.search(text)
                if m_goto and m_goto.group(1) not in visited:
                    queue.append(m_goto.group(1))
                m_label = label_pattern.search(text)
                if m_label and m_label.group(1) not in visited:
                    queue.append(m_label.group(1))
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
            manifest[classname] = info
        return manifest

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
        checks: List[int] = []
        sets: List[int] = []
        clears: List[int] = []
        assignments: List[int] = []
        for line in block:
            if "0x11c" not in line:
                continue
            # direct assignment
            m_assign = re.search(r"\*\([^)]*0x11c\)\s*=\s*(0x[0-9a-f]+|\d+)", line, re.IGNORECASE)
            if m_assign:
                assignments.append(int(m_assign.group(1), 0))
            # |= sets
            for m in re.finditer(r"\|=\s*(0x[0-9a-f]+|\d+)", line, re.IGNORECASE):
                value = int(m.group(1), 0)
                if value not in sets:
                    sets.append(value)
            # &= clears via mask
            for m in re.finditer(r"&=\s*(0x[0-9a-f]+|\d+)", line, re.IGNORECASE):
                mask = int(m.group(1), 0) & 0xFFFFFFFF
                cleared = (~mask) & 0xFFFFFFFF
                if 0 < cleared < 0xFFFFFFFF and cleared not in clears:
                    clears.append(cleared)
            # explicit masked assignment clears
            m_clear = re.search(
                r"\*\([^)]*0x11c\)\s*=\s*\*\([^)]*0x11c\)\s*&\s*(0x[0-9a-f]+|\d+)",
                line,
                re.IGNORECASE,
            )
            if m_clear:
                mask = int(m_clear.group(1), 0) & 0xFFFFFFFF
                cleared = (~mask) & 0xFFFFFFFF
                if 0 < cleared < 0xFFFFFFFF and cleared not in clears:
                    clears.append(cleared)
            # remaining & occurrences are treated as checks
            for m in re.finditer(r"\([^)]*0x11c[^)]*\)\s*&\s*(0x[0-9a-f]+|\d+)", line, re.IGNORECASE):
                value = int(m.group(1), 0)
                if value not in checks:
                    checks.append(value)
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
        return spawn_map

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
        assign_pattern = re.compile(r"self->([a-zA-Z0-9_\.]+)\s*=\s*([-+]?[0-9]*\.?[0-9]+f?|0x[0-9a-fA-F]+)")
        for line in lines:
            match = assign_pattern.search(line)
            if not match:
                continue
            field, raw_val = match.groups()
            value: float
            if raw_val.lower().startswith("0x"):
                value = float(int(raw_val, 16))
            elif raw_val.endswith("f"):
                value = float(raw_val[:-1])
            else:
                value = float(raw_val)
            defaults[field] = value
        return defaults

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
