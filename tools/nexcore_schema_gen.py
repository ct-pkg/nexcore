#!/usr/bin/env python3
"""nexcore-gen-schema — P3 生成器骨架：Schema -> 四类产物 + 负例 + 漂移检查。

输入：`tests/fixture.schema.yaml`（框架仓库；宿主仓库换成 `schema/nexcore.schema.yaml`）
输出：`schema/generated/{c,swift,kotlin,js}/`（四类产物，全部入库）

原则（与 docs/codegen.md GEN-xx 对应）：
  * 先建模型再发射：所有标识符都要过命名与冲突检查，不允许“原样输出”；
  * 报错必须带 `文件:行号` 与规则号（GEN-xx），负例靠它做二值判定；
  * 生成物不含时间戳/绝对路径/随机顺序（GEN-09），同输入两次字节一致；
  * `--check` 只比对不写盘，漂移 rc=2（不许“重新生成一遍就绿”）。

规则子集（骨架阶段）：
  GEN-01 文件头带 schema_hash 与 DO NOT EDIT
  GEN-06 形状/命名/枚举值合法（含未知键、保留前缀、长度上限）
  GEN-07 类型白名单（禁止 any/dynamic/object/JsonElement 之类）
  GEN-12 方法集合非空，且四类产物各声明一次
  GEN-13 标识符冲突是错误，不自动重命名
  GEN-16 trait 禁止跨 ABI（`use`），含按值嵌套的递归检查
  GEN-20 重复 key（(ns, trait, method) 与 record 字段）
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    import yaml
except ImportError:  # 缺依赖时不得装绿：用退出码 2 明确“跑不了”
    print(
        "nexcore-gen-schema: 需要 PyYAML（python3 -m pip install --user 'pyyaml==6.0.1'）",
        file=sys.stderr,
    )
    sys.exit(2)

GENERATOR_ID = "nexcore-gen-schema 0.1.0"
MAX_METHOD_BYTES = 128  # NEXCORE_MAX_METHOD_BYTES（abi/abi-layout.txt 已锁）
MAX_PARAMS = 8
IDENT_RE = r"^[a-z][a-z0-9_]*$"
RECORD_RE = r"^[A-Z][A-Za-z0-9]*$"
RESERVED_NAMESPACES = {"nexcore", "abi", "gen"}
RESERVED_PREFIXES = ("nexcore_", "nx_gen_")

PRIMITIVES = {
    "string": {"c": "const char *", "swift": "String", "kotlin": "String", "js": "string"},
    "int": {"c": "int32_t", "swift": "Int32", "kotlin": "Int", "js": "number"},
    "bool": {"c": "int32_t", "swift": "Bool", "kotlin": "Boolean", "js": "boolean"},
    "double": {"c": "double", "swift": "Double", "kotlin": "Double", "js": "number"},
    "bytes": {"c": "const void *", "swift": "Data", "kotlin": "ByteArray", "js": "string"},
}
BANNED_TYPES = {"any", "dynamic", "object", "json", "jsonelement", "jsonobject", "jsonarray"}


class GenError(Exception):
    def __init__(self, rule: str, line: int, message: str):
        super().__init__(message)
        self.rule = rule
        self.line = line
        self.message = message


# ---------------------------------------------------------------------------
# YAML -> 带行号的模型（用 compose 而不是 safe_load：要保留 key 的原始行号与重复 key）
# ---------------------------------------------------------------------------


@dataclass
class Scalar:
    value: str
    tag: str
    line: int


@dataclass
class Seq:
    items: list
    line: int


@dataclass
class Map:
    pairs: list  # [(key, key_line, node)]
    line: int

    def get(self, key):
        for name, line, node in self.pairs:
            if name == key:
                return line, node
        return None

    def keys(self):
        return [name for name, _, _ in self.pairs]

    def duplicates(self):
        seen, dup = set(), []
        for name, line, _ in self.pairs:
            if name in seen:
                dup.append((name, line))
            seen.add(name)
        return dup


def compose_child(node):
    if isinstance(node, yaml.MappingNode):
        pairs = []
        for key_node, value_node in node.value:
            pairs.append(
                (key_node.value, key_node.start_mark.line + 1, compose_child(value_node))
            )
        return Map(pairs, node.start_mark.line + 1)
    if isinstance(node, yaml.SequenceNode):
        return Seq([compose_child(item) for item in node.value], node.start_mark.line + 1)
    node_tag = getattr(node, "tag", "")
    return Scalar(node.value, node_tag, node.start_mark.line + 1)


def load_yaml(path: Path):
    text = path.read_text(encoding="utf-8")
    try:
        node = yaml.compose(text, Loader=yaml.SafeLoader)
    except yaml.YAMLError as exc:
        mark = getattr(exc, "problem_mark", None)
        line = mark.line + 1 if mark is not None else 1
        problem = getattr(exc, "problem", None) or str(exc).splitlines()[0]
        raise GenError("GEN-06", line, f"YAML 解析失败: {problem}")
    if node is None:
        raise GenError("GEN-06", 1, "Schema 为空")
    return compose_child(node)


# ---------------------------------------------------------------------------
# 形状与命名校验
# ---------------------------------------------------------------------------


def as_map(node, what: str) -> Map:
    if not isinstance(node, Map):
        raise GenError("GEN-06", node.line, f"{what} 必须是映射")
    return node


def as_seq(node, what: str) -> Seq:
    if not isinstance(node, Seq):
        raise GenError("GEN-06", node.line, f"{what} 必须是列表")
    return node


def as_str(node, what: str) -> str:
    if not isinstance(node, Scalar):
        raise GenError("GEN-06", node.line, f"{what} 必须是标量")
    if node.value is None:
        raise GenError("GEN-06", node.line, f"{what} 不能为空")
    return node.value


def reject_unknown_keys(mapping: Map, allowed: set, what: str) -> None:
    for name, line in mapping.duplicates():
        raise GenError("GEN-13", line, f"{what} 出现重复键 {name!r}")
    for name, line, _ in mapping.pairs:
        if name not in allowed:
            raise GenError("GEN-06", line, f"{what} 出现未知键 {name!r}（允许: {sorted(allowed)}）")


def check_identifier(name: str, line: int, what: str) -> None:
    if not re.match(IDENT_RE, name):
        raise GenError("GEN-06", line, f"{what} {name!r} 不是合法标识符（{IDENT_RE}）")
    if len(name.encode("utf-8")) > MAX_METHOD_BYTES:
        raise GenError(
            "GEN-06", line, f"{what} {name!r} 超过 {MAX_METHOD_BYTES} 字节上限"
        )
    for prefix in RESERVED_PREFIXES:
        if name.startswith(prefix):
            raise GenError("GEN-06", line, f"{what} {name!r} 使用了生成器/ABI 保留前缀")


def check_type(raw: str, line: int, what: str, record_names: set) -> str:
    lowered = raw.lower()
    if lowered in BANNED_TYPES:
        raise GenError("GEN-07", line, f"{what} 类型 {raw!r} 在禁止名单（GEN-07：无 Any/JSON 擦除）")
    if raw in PRIMITIVES or raw in record_names:
        return raw
    raise GenError(
        "GEN-07", line, f"{what} 类型 {raw!r} 未知（允许: {sorted(PRIMITIVES)} 或已声明 record）"
    )


@dataclass(frozen=True)
class Param:
    name: str
    type: str


@dataclass(frozen=True)
class Method:
    name: str
    direction: str
    params: tuple
    result: str


@dataclass(frozen=True)
class Record:
    name: str
    fields: tuple


@dataclass(frozen=True)
class Schema:
    namespace: str
    records: tuple
    methods: tuple
    schema_hash: str


def parse_method(node, index: int, record_names: set) -> Method:
    mapping = as_map(node, f"methods[{index}]")
    reject_unknown_keys(mapping, {"name", "direction", "params", "result"}, f"methods[{index}]")

    name_item = mapping.get("name")
    if name_item is None:
        raise GenError("GEN-06", mapping.line, f"methods[{index}] 缺 name")
    name_line, name_node = name_item
    name = as_str(name_node, f"methods[{index}].name")
    check_identifier(name, name_line, f"methods[{index}].name")

    dir_item = mapping.get("direction")
    if dir_item is None:
        raise GenError("GEN-06", mapping.line, f"methods[{index}] 缺 direction")
    dir_line, dir_node = dir_item
    direction = as_str(dir_node, f"methods[{index}].direction")
    if direction not in ("ui2js", "js2ui"):
        raise GenError("GEN-06", dir_line, f"direction {direction!r} 必须是 ui2js 或 js2ui")

    result_item = mapping.get("result")
    if result_item is None:
        raise GenError("GEN-06", mapping.line, f"methods[{index}] 缺 result")
    result_line, result_node = result_item
    result = check_type(
        as_str(result_node, f"methods[{index}].result"),
        result_line,
        f"methods[{index}].result",
        record_names,
    )

    params_item = mapping.get("params")
    if params_item is None:
        raise GenError("GEN-06", mapping.line, f"methods[{index}] 缺 params（空列表也要写）")
    params_line, params_node = params_item
    params_seq = as_seq(params_node, f"methods[{index}].params")
    if len(params_seq.items) > MAX_PARAMS:
        raise GenError(
            "GEN-06", params_line, f"methods[{index}].params 超过 {MAX_PARAMS} 个参数"
        )

    params = []
    seen = {}
    for p_index, item in enumerate(params_seq.items):
        param_map = as_map(item, f"methods[{index}].params[{p_index}]")
        reject_unknown_keys(
            param_map, {"name", "type"}, f"methods[{index}].params[{p_index}]"
        )
        p_name_item = param_map.get("name")
        if p_name_item is None:
            raise GenError("GEN-06", param_map.line, f"params[{p_index}] 缺 name")
        p_name_line, p_name_node = p_name_item
        p_name = as_str(p_name_node, f"params[{p_index}].name")
        check_identifier(p_name, p_name_line, f"methods[{index}].params[{p_index}].name")
        if p_name in seen:
            raise GenError(
                "GEN-13",
                p_name_line,
                f"方法 {name!r} 的参数名 {p_name!r} 与第 {seen[p_name]} 个参数冲突",
            )
        seen[p_name] = p_index
        p_type_item = param_map.get("type")
        if p_type_item is None:
            raise GenError("GEN-06", param_map.line, f"params[{p_index}] 缺 type")
        p_type_line, p_type_node = p_type_item
        p_type = check_type(
            as_str(p_type_node, f"params[{p_index}].type"),
            p_type_line,
            f"methods[{index}].params[{p_index}].type",
            record_names,
        )
        params.append(Param(p_name, p_type))

    return Method(name, direction, tuple(params), result)


def parse_schema(path: Path) -> Schema:
    root = as_map(load_yaml(path), "根节点")
    reject_unknown_keys(root, {"schema_version", "namespace", "records", "methods"}, "根节点")

    version_item = root.get("schema_version")
    if version_item is None:
        raise GenError("GEN-06", root.line, "缺少 schema_version")
    version_line, version_node = version_item
    if not isinstance(version_node, Scalar) or version_node.value != "1":
        raise GenError("GEN-06", version_line, "schema_version 必须是整数 1")

    ns_item = root.get("namespace")
    if ns_item is None:
        raise GenError("GEN-06", root.line, "缺少 namespace")
    ns_line, ns_node = ns_item
    namespace = as_str(ns_node, "namespace")
    check_identifier(namespace, ns_line, "namespace")
    if namespace in RESERVED_NAMESPACES:
        raise GenError("GEN-06", ns_line, f"namespace {namespace!r} 是保留名")

    # records 先建名字集合，字段里的 record 引用才能递归判定
    records_node = root.get("records")
    raw_records = []
    record_names: set = set()
    if records_node is not None:
        _, node = records_node
        for index, item in enumerate(as_seq(node, "records").items):
            mapping = as_map(item, f"records[{index}]")
            name_item = mapping.get("name")
            if name_item is None:
                raise GenError("GEN-06", mapping.line, f"records[{index}] 缺 name")
            name_line, name_node = name_item
            rec_name = as_str(name_node, f"records[{index}].name")
            if not re.match(RECORD_RE, rec_name):
                raise GenError(
                    "GEN-06", name_line, f"record 名 {rec_name!r} 必须是 PascalCase（{RECORD_RE}）"
                )
            if rec_name in record_names:
                raise GenError("GEN-13", name_line, f"record 名 {rec_name!r} 重复")
            record_names.add(rec_name)
            raw_records.append((index, rec_name, mapping))

    records = []
    refs: dict = {}
    trait_records: dict = {}
    for index, rec_name, mapping in raw_records:
        reject_unknown_keys(mapping, {"name", "fields", "use"}, f"records[{index}]")
        if mapping.get("use") is not None:
            use_line, _ = mapping.get("use")
            trait_records[rec_name] = use_line
        fields_item = mapping.get("fields")
        if fields_item is None:
            raise GenError("GEN-06", mapping.line, f"records[{index}] 缺 fields")
        fields = []
        seen_fields = {}
        for f_index, item in enumerate(as_seq(fields_item[1], f"records[{index}].fields").items):
            field_map = as_map(item, f"records[{index}].fields[{f_index}]")
            reject_unknown_keys(
                field_map, {"name", "type"}, f"records[{index}].fields[{f_index}]"
            )
            f_name_item = field_map.get("name")
            if f_name_item is None:
                raise GenError("GEN-06", field_map.line, f"fields[{f_index}] 缺 name")
            f_name_line, f_name_node = f_name_item
            f_name = as_str(f_name_node, f"fields[{f_index}].name")
            check_identifier(f_name, f_name_line, f"records[{index}].fields[{f_index}].name")
            if f_name in seen_fields:
                raise GenError(
                    "GEN-13",
                    f_name_line,
                    f"record 字段 {f_name!r} 与第 {seen_fields[f_name]} 个字段冲突",
                )
            seen_fields[f_name] = f_index
            f_type_item = field_map.get("type")
            if f_type_item is None:
                raise GenError("GEN-06", field_map.line, f"fields[{f_index}] 缺 type")
            f_type_line, f_type_node = f_type_item
            f_type = check_type(
                as_str(f_type_node, f"fields[{f_index}].type"),
                f_type_line,
                f"records[{index}].fields[{f_index}].type",
                record_names,
            )
            fields.append((f_name, f_type, f_type_line))
        refs[rec_name] = [f_type for _, f_type, _ in fields if f_type in record_names]
        records.append(Record(rec_name, tuple((n, t) for n, t, _ in fields)))

    # 先查“按值嵌套了带 trait 的 record”（ACC-A-03 的递归版本），再查 record 自己带 use。
    # 顺序有意为之：嵌套负例要报在**引用方字段行**，而不是被引用 record 的 use 行。
    for index, rec_name, mapping in raw_records:
        for f_index, item in enumerate(as_seq(mapping.get("fields")[1], "fields").items):
            field_map = as_map(item, f"records[{index}].fields[{f_index}]")
            f_name_line, f_name_node = field_map.get("name")
            f_name = as_str(f_name_node, "name")
            f_type_line, f_type_node = field_map.get("type")
            f_type = as_str(f_type_node, "type")
            stack, seen = [f_type], set()
            while stack:
                cursor = stack.pop()
                if cursor in seen:
                    continue
                seen.add(cursor)
                if cursor in trait_records:
                    raise GenError(
                        "GEN-16",
                        f_type_line,
                        f"records[{index}].{f_name} 按值嵌套了带 trait 的 record {cursor}",
                    )
                stack.extend(refs.get(cursor, []))
    if trait_records:
        first = min(trait_records.items(), key=lambda kv: kv[1])
        raise GenError(
            "GEN-16",
            first[1],
            f"record {first[0]!r} 使用 'use'：trait 禁止跨 ABI（会追加隐藏 vptr）",
        )

    methods = []
    seen_methods = {}
    methods_item = root.get("methods")
    if methods_item is None:
        raise GenError("GEN-06", root.line, "缺少 methods")
    methods_seq = as_seq(methods_item[1], "methods")
    if not methods_seq.items:
        raise GenError("GEN-12", methods_item[0], "methods 为空：无法生成四类产物")
    for index, item in enumerate(methods_seq.items):
        method = parse_method(item, index, record_names)
        if method.name in seen_methods:
            raise GenError(
                "GEN-20",
                item.line if isinstance(item, Map) else methods_seq.line,
                f"方法 {method.name!r} 重复（(ns, trait, method) key 冲突）",
            )
        seen_methods[method.name] = index
        methods.append(method)

    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return Schema(namespace, tuple(records), tuple(methods), digest)


# ---------------------------------------------------------------------------
# 发射四类产物
# ---------------------------------------------------------------------------


def header(prefix: str, suffix: str, schema: Schema, source: Path) -> list:
    return [
        f"{prefix} GENERATED FROM {source.as_posix()} -- DO NOT EDIT.{suffix}",
        f"{prefix} generator: {GENERATOR_ID}  schema_hash: {schema.schema_hash}{suffix}",
    ]


def pascal(name: str) -> str:
    return "".join(part.capitalize() for part in name.split("_"))


def c_wrapper_name(schema: Schema, method: Method) -> str:
    return f"nx_gen_{schema.namespace}_{method.name}"


def c_params(method: Method) -> str:
    parts = [f"{PRIMITIVES[p.type]['c']}{p.name}" for p in method.params]
    parts.append("char **out")
    parts.append("size_t *out_len")
    return ", ".join(parts)


def render_c_header(schema: Schema, source: Path) -> str:
    guard = f"NEXCORE_GENERATED_{schema.namespace.upper()}_REGISTRY_H"
    lines = header("/*", " */", schema, source) + [
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "NexCore.h"',
        "",
        "typedef enum {",
        "    NX_GEN_DIR_UI2JS = 0,",
        "    NX_GEN_DIR_JS2UI = 1",
        "} nx_gen_dir;",
        "",
        "struct nx_gen_method {",
        "    const char *name;",
        "    nx_gen_dir direction;",
        "    const char *result_type;",
        "    size_t param_count;",
        "};",
        "",
    ]
    for method in schema.methods:
        lines.append(f"int32_t {c_wrapper_name(schema, method)}({c_params(method)});")
    lines += [
        "",
        "extern const struct nx_gen_method *const nx_gen_methods;",
        "extern const size_t nx_gen_method_count;",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    return "\n".join(lines)


def render_c_source(schema: Schema, source: Path) -> str:
    lines = header("/*", " */", schema, source) + [
        "",
        '#include "nexcore_registry.h"',
        "",
        "static const struct nx_gen_method nx_gen_method_table[] = {",
    ]
    rows = []
    for method in schema.methods:
        rows.append(
            f'    {{"{method.name}", NX_GEN_DIR_{method.direction.upper()}, '
            f'"{method.result}", {len(method.params)}}}'
        )
    lines.append(",\n".join(rows))
    lines += [
        "};",
        "",
        "const struct nx_gen_method *const nx_gen_methods = nx_gen_method_table;",
        "const size_t nx_gen_method_count =",
        "    sizeof(nx_gen_method_table) / sizeof(nx_gen_method_table[0]);",
        "",
    ]
    for method in schema.methods:
        lines += [
            f"/* {schema.namespace}::{method.name} —— 骨架阶段的 wrapper：业务 handler 未接，"
            f"统一返回 NX_E_INTERNAL。 */",
            f"int32_t {c_wrapper_name(schema, method)}({c_params(method)})",
            "{",
        ]
        for param in method.params:
            lines.append(f"    (void){param.name};")
        lines += [
            "    (void)out;",
            "    (void)out_len;",
            "    return NX_E_INTERNAL;",
            "}",
            "",
        ]
    return "\n".join(lines)


def render_swift(schema: Schema, source: Path) -> str:
    type_name = f"NexCore{pascal(schema.namespace)}"
    lines = header("//", "", schema, source) + [
        "import Foundation",
        "",
        f"public enum {type_name}Method: String, CaseIterable {{",
    ]
    for method in schema.methods:
        lines.append(f'    case {method.name} = "{method.name}"')
    lines += ["}", "", "@MainActor", f"public protocol {type_name}Handler {{"]
    for method in schema.methods:
        params = ", ".join(f"{p.name}: {PRIMITIVES[p.type]['swift']}" for p in method.params)
        lines.append(f"    // direction: {method.direction}")
        lines.append(f"    func {method.name}({params}) throws -> {PRIMITIVES[method.result]['swift']}")
    lines += ["}", ""]
    return "\n".join(lines)


def render_kotlin(schema: Schema, source: Path) -> str:
    type_name = f"NexCore{pascal(schema.namespace)}"
    lines = header("//", "", schema, source) + [
        f"package nexcore.{schema.namespace}",
        "",
        f"enum class {type_name}Method(val wire: String) {{",
    ]
    rows = [f'    {m.name}("{m.name}")' for m in schema.methods]
    lines.append(",\n".join(rows))
    lines += ["}", "", f"interface {type_name}Handler {{"]
    for method in schema.methods:
        params = ", ".join(f"{p.name}: {PRIMITIVES[p.type]['kotlin']}" for p in method.params)
        lines.append(f"    // direction: {method.direction}")
        lines.append(f"    fun {method.name}({params}): {PRIMITIVES[method.result]['kotlin']}")
    lines += ["}", ""]
    return "\n".join(lines)


def render_js(schema: Schema, source: Path) -> str:
    lines = header("//", "", schema, source) + [
        "",
        f'export const schemaHash = "{schema.schema_hash}";',
        "",
        "export const methods = Object.freeze({",
    ]
    rows = []
    for method in schema.methods:
        params = ", ".join(f'"{p.name}"' for p in method.params)
        rows.append(
            f'  {method.name}: Object.freeze({{ direction: "{method.direction}", '
            f'result: "{method.result}", params: Object.freeze([{params}]) }})'
        )
    lines.append(",\n".join(rows))
    lines += ["});", "", "export function createHandler(impl) {", "  return Object.freeze({"]
    rows = []
    for method in schema.methods:
        args = ", ".join(p.name for p in method.params)
        rows.append(f"    {method.name}({args}) {{ return impl.{method.name}({args}); }}")
    lines.append(",\n".join(rows))
    lines += ["  });", "}", ""]
    return "\n".join(lines)


def artifacts(schema: Schema, source: Path) -> dict:
    ns = schema.namespace
    return {
        f"c/nexcore_registry.h": render_c_header(schema, source),
        f"c/nexcore_registry.c": render_c_source(schema, source),
        f"swift/NexCore{pascal(ns)}.swift": render_swift(schema, source),
        f"kotlin/NexCore{pascal(ns)}.kt": render_kotlin(schema, source),
        f"js/nexcore_{ns}.js": render_js(schema, source),
    }


# ---------------------------------------------------------------------------
# 负例 runner：每个 fixture 必须“真红”，且规则号与行号对得上
# ---------------------------------------------------------------------------


def run_negatives(root: Path, expect_path: Path) -> int:
    rows = []
    for line in expect_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            print(f"⑭ FAIL: expected.tsv 行格式不对: {line!r}", file=sys.stderr)
            return 1
        name, rule, expected_line = parts[0], parts[1], int(parts[2])
        rows.append((name, rule, expected_line))

    failures = 0
    for name, rule, expected_line in rows:
        path = root / name
        shown = f"{root.as_posix()}/{name}"
        if not path.is_file():
            print(f"⑭ FAIL: 缺负例夹具 {path}", file=sys.stderr)
            failures += 1
            continue
        try:
            parse_schema(path)
        except GenError as exc:
            if exc.rule != rule or exc.line != expected_line:
                print(
                    f"⑭ FAIL: {shown} 期望 {rule}@{expected_line}，"
                    f"实际 {exc.rule}@{exc.line}（{exc.message}）",
                    file=sys.stderr,
                )
                failures += 1
                continue
            if not exc.message:
                print(f"⑭ FAIL: {shown} 报错缺少说明", file=sys.stderr)
                failures += 1
                continue
            print(f"  red {shown}:{exc.line}: {exc.rule} {exc.message}")
        else:
            print(f"⑭ FAIL: {shown} 本该被拒绝，却生成成功（负例不红）", file=sys.stderr)
            failures += 1

    if failures:
        print(f"⑭ FAIL: {failures}/{len(rows)} 条负例不符合预期", file=sys.stderr)
        return 1
    print(f"⑭ schema negatives: PASS ({len(rows)}/{len(rows)} 条均为真红，规则与行号匹配)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--schema", default="tests/fixture.schema.yaml")
    ap.add_argument("--out", default="schema/generated")
    ap.add_argument("--check", action="store_true", help="只比对，不写盘；漂移 rc=2")
    ap.add_argument("--write", action="store_true", help="写盘（人工确认后）")
    ap.add_argument("--negatives", default=None, help="负例夹具目录")
    ap.add_argument("--expect", default=None, help="负例期望表（file<TAB>rule<TAB>line）")
    ap.add_argument("--dump-hash", action="store_true")
    args = ap.parse_args()

    source = Path(args.schema)
    if not source.is_file():
        print(f"nexcore-gen-schema: 缺 Schema {source}", file=sys.stderr)
        return 1

    if args.negatives:
        expect = Path(args.expect or (Path(args.negatives) / "expected.tsv"))
        return run_negatives(Path(args.negatives), expect)

    if args.dump_hash:
        print(hashlib.sha256(source.read_bytes()).hexdigest())
        return 0

    try:
        schema = parse_schema(source)
    except GenError as exc:
        print(f"{source.as_posix()}:{exc.line}: {exc.rule} {exc.message}", file=sys.stderr)
        return 1

    files = artifacts(schema, source)
    out_root = Path(args.out)

    if args.write:
        for rel, text in files.items():
            path = out_root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
            print(f"wrote {path}")
        return 0

    if not args.check:
        print("nexcore-gen-schema: 需要 --check / --write / --negatives", file=sys.stderr)
        return 2

    drift = 0
    for rel, text in files.items():
        path = out_root / rel
        existing = path.read_text(encoding="utf-8") if path.is_file() else ""
        if existing != text:
            print(f"drift: {path}", file=sys.stderr)
            drift += 1
    if drift:
        print(
            f"nexcore-gen-schema: {drift}/{len(files)} 个生成物漂移（手改或 Schema 变更未重生成）",
            file=sys.stderr,
        )
        return 2
    print(f"nexcore-gen-schema: PASS（{len(files)} 个产物与 Schema 一致）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
