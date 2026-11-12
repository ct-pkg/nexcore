#!/usr/bin/env python3
"""nexcore-gen — b' 拓扑的映射单元生成器（骨架版）。

输入：src/units.json（单元清单 + 每个命名空间要暴露的 trait 方法）
输出：src/<ns>/<ns>_map.m.ct

五条机械检查里，④（recipe callee == 符号表 cname）必须留在生成器内部：
--verify-ct 会解析已生成的 map 文件与 CT 导出的 C 头，做结构化断言，
不依赖外部 grep。其余四条由 tools/check.sh 调用脚本/编译器完成。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

GENERATOR_ID = "nexcore-gen 0.1.0"

DECL_RE = re.compile(
    r"\bpub\s+([^\n;()]+?)\s*([A-Za-z_]\w*)\s*\(([^)]*)\)\s*;"
)
MAP_BLOCK_RE = re.compile(
    r"map\s+trait\s+([A-Za-z_]\w*)\s+for\s+namespace\s+([A-Za-z_:]\w*)\s*\{(.*?)\}",
    re.DOTALL,
)
RECIPE_RE = re.compile(
    r"([A-Za-z_]\w*)\s*\(([^)]*)\)\s*->\s*([A-Za-z_]\w*)\s*\(([^)]*)\)\s*;"
)
PROTO_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_\s\*]*?)\s+([A-Za-z_]\w*)\s*\([^;]*\)\s*;\s*$"
)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


@dataclass(frozen=True)
class Param:
    type: str
    name: str


@dataclass(frozen=True)
class Symbol:
    namespace: str
    name: str
    cname: str
    returns: str
    params: tuple[Param, ...]


@dataclass(frozen=True)
class Recipe:
    method: str
    callee: str
    args: tuple[str, ...]


@dataclass(frozen=True)
class Unit:
    namespace: str
    trait: str
    source: str
    methods: tuple[str, ...]


def parse_params(raw: str) -> tuple[Param, ...]:
    raw = raw.strip()
    if raw == "" or raw == "void":
        return ()
    params: list[Param] = []
    for part in raw.split(","):
        part = part.strip()
        m = re.match(r"^(.*?)\s*([A-Za-z_]\w*)$", part)
        if not m or not m.group(1).strip():
            raise SystemExit(f"nexcore-gen: 无法解析参数: {part!r}")
        params.append(Param(type=m.group(1).strip(), name=m.group(2)))
    return tuple(params)


def load_spec(path: Path) -> tuple[Unit, ...]:
    data = json.loads(path.read_text(encoding="utf-8"))
    units = []
    seen_ns: set[str] = set()
    for item in data["units"]:
        ns = item["namespace"]
        if ns in seen_ns:
            raise SystemExit(f"nexcore-gen: 重复命名空间 {ns}")
        seen_ns.add(ns)
        units.append(
            Unit(
                namespace=ns,
                trait=item["trait"],
                source=item["source"],
                methods=tuple(item["methods"]),
            )
        )
    return tuple(units)


def build_symbols(src_root: Path, units: tuple[Unit, ...]) -> dict[tuple[str, str], Symbol]:
    symbols: dict[tuple[str, str], Symbol] = {}
    seen_cnames: set[str] = set()
    for unit in units:
        path = src_root / unit.source
        if not path.is_file():
            raise SystemExit(f"nexcore-gen: 源文件不存在: {path}")
        text = strip_comments(path.read_text(encoding="utf-8"))
        ns_match = re.search(r"\bnamespace\s+([A-Za-z_:]\w*)\s*;", text)
        if not ns_match:
            raise SystemExit(f"nexcore-gen: {path} 缺少 namespace 声明")
        if ns_match.group(1) != unit.namespace:
            raise SystemExit(
                f"nexcore-gen: {path} 的 namespace {ns_match.group(1)!r} "
                f"与清单 {unit.namespace!r} 不一致"
            )
        for decl in DECL_RE.finditer(text):
            returns = " ".join(decl.group(1).split())
            name = decl.group(2)
            cname = f"{unit.namespace}_{name}"
            if cname in seen_cnames:
                raise SystemExit(f"nexcore-gen: cname 重复: {cname}")
            seen_cnames.add(cname)
            symbols[(unit.namespace, name)] = Symbol(
                namespace=unit.namespace,
                name=name,
                cname=cname,
                returns=returns,
                params=parse_params(decl.group(3)),
            )
    return symbols


def recipe_for(symbol: Symbol) -> Recipe:
    # recipe 是结构化模型，不是字符串拼接后的正则产物。
    return Recipe(
        method=symbol.name,
        callee=symbol.cname,
        args=tuple(p.name for p in symbol.params),
    )


def render_unit(unit: Unit, symbols: dict[tuple[str, str], Symbol]) -> str:
    recipes: list[Recipe] = []
    for method in unit.methods:
        symbol = symbols.get((unit.namespace, method))
        if symbol is None:
            raise SystemExit(
                f"nexcore-gen: SYM-01 失败：命名空间 {unit.namespace} 中找不到 {method}"
            )
        recipes.append(recipe_for(symbol))

    lines = [
        "/* GENERATED FROM src/units.json -- DO NOT EDIT. */",
        f"/* generator: {GENERATOR_ID} */",
        "",
        f'#include "{unit.namespace}.h"',
        "",
        f"namespace {unit.namespace};",
        "",
        f"pub trait {unit.trait} {{",
    ]
    for rec in recipes:
        symbol = symbols[(unit.namespace, rec.method)]
        sig = ", ".join(f"{p.type} {p.name}" for p in symbol.params) or "void"
        lines.append(f"    {symbol.returns} {rec.method}({sig});")
    lines += ["}", "", f"map trait {unit.trait} for namespace {unit.namespace} {{"]
    for rec in recipes:
        lines.append(
            f"    {rec.method}({', '.join(rec.args)}) -> "
            f"{rec.callee}({', '.join(rec.args)});"
        )
    lines += ["}", ""]
    return "\n".join(lines)


def map_path(src_root: Path, unit: Unit) -> Path:
    return src_root / unit.namespace / f"{unit.namespace}_map.m.ct"


def parse_map(path: Path) -> tuple[str, str, list[Recipe]]:
    text = strip_comments(path.read_text(encoding="utf-8"))
    block = MAP_BLOCK_RE.search(text)
    if not block:
        raise SystemExit(f"nexcore-gen: {path} 中找不到 map trait 块")
    trait, ns, body = block.group(1), block.group(2), block.group(3)
    recipes = []
    for m in RECIPE_RE.finditer(body):
        args = tuple(a.strip() for a in m.group(4).split(",") if a.strip())
        recipes.append(Recipe(method=m.group(1), callee=m.group(3), args=args))
    return trait, ns, recipes


def emitted_symbols(ct_out: Path) -> set[str]:
    if not ct_out.is_dir():
        raise SystemExit(f"nexcore-gen: --verify-ct 目录不存在: {ct_out}")
    symbols: set[str] = set()
    for header in sorted(ct_out.glob("*.h")):
        for line in header.read_text(encoding="utf-8").splitlines():
            if "typedef" in line or "(*" in line:
                continue
            m = PROTO_RE.match(line)
            if m:
                symbols.add(m.group(2))
    return symbols


def verify(units: tuple[Unit, ...], src_root: Path, ct_out: Path) -> int:
    """检查 ④：解析已落盘的 map，断言 recipe callee 等于符号表 cname 且符号真实存在。"""
    symbols = build_symbols(src_root, units)
    ct_symbols = emitted_symbols(ct_out)
    checked = 0
    for unit in units:
        path = map_path(src_root, unit)
        if not path.is_file():
            print(f"④ FAIL: 缺少映射单元 {path}", file=sys.stderr)
            return 1
        trait, ns, recipes = parse_map(path)
        if trait != unit.trait or ns != unit.namespace:
            print(
                f"④ FAIL: {path} 的 trait/namespace ({trait}, {ns}) "
                f"与清单 ({unit.trait}, {unit.namespace}) 不一致",
                file=sys.stderr,
            )
            return 1
        by_method = {r.method: r for r in recipes}
        for method in unit.methods:
            symbol = symbols[(unit.namespace, method)]
            rec = by_method.get(method)
            if rec is None:
                print(f"④ FAIL: {path} 缺少 recipe {method}", file=sys.stderr)
                return 1
            if rec.callee != symbol.cname:
                print(
                    f"④ FAIL: {path} recipe {method} 的 callee {rec.callee!r} "
                    f"!= 符号表 cname {symbol.cname!r}",
                    file=sys.stderr,
                )
                return 1
            if rec.callee not in ct_symbols:
                print(
                    f"④ FAIL: {path} recipe {method} 的 callee {rec.callee!r} "
                    f"未出现在 {ct_out}/*.h 的导出原型中",
                    file=sys.stderr,
                )
                return 1
            checked += 1
    print(f"④ recipe callee == symbol cname: PASS ({checked} recipes)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", default="src/units.json")
    ap.add_argument("--src", default="src")
    ap.add_argument("--check", action="store_true", help="只比对，不写盘；漂移返回 2")
    ap.add_argument("--verify-ct", default=None, help="CT 导出目录，执行检查 ④")
    args = ap.parse_args()

    src_root = Path(args.src)
    units = load_spec(Path(args.spec))
    symbols = build_symbols(src_root, units)

    if args.verify_ct:
        return verify(units, src_root, Path(args.verify_ct))

    drift = 0
    for unit in units:
        path = map_path(src_root, unit)
        content = render_unit(unit, symbols)
        if args.check:
            existing = path.read_text(encoding="utf-8") if path.is_file() else ""
            if existing != content:
                print(f"drift: {path}", file=sys.stderr)
                drift += 1
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        print(f"wrote {path}")

    if args.check:
        if drift:
            print(f"nexcore-gen: {drift} 个文件漂移", file=sys.stderr)
            return 2
        print("generator drift: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
