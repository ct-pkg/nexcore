#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# tools/check.sh —— 构建 + 机械检查的唯一入口
#
# 归属约定（2026-11-06 定，改动前先读这一段）
#   * 本文件由【编码侧】唯一维护并直接编辑；编码侧负责保证全绿。
#   * 【需求侧】只提“检查项需求”（要检什么、允许什么、负例是什么、报什么错），
#     不直接编辑本文件；需要新增/修改检查时，在需求里写明编号与断言，
#     由编码侧落盘。
#   * 双方同时在改时：以编码侧最后一次落盘的版本为准；发现非编码侧改动，
#     先冻结该文件、对齐需求，再并回一版，不做无记录的双向覆盖。
#   * 新增检查项使用新编号，不复用旧编号；编号表见文件末尾注释。
#
# 检查项：0 / ①a ①b ①c / ②a ②b ②c / ③ / ④ / ⑤a ⑤b / ⑥ / ⑦ / ⑧ / ⑨ / smoke
# ---------------------------------------------------------------------------
set -euo pipefail

cd "$(dirname "$0")/.."

SRC=src
ABI=build/abi
IMPL=build/impl
BIN=build/bin
QJS=experiments/third_party/quickjs
CC=${CC:-cc}
CFLAGS=(-std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration -Werror=incompatible-pointer-types)

rm -rf "$ABI" "$IMPL" "$BIN"
mkdir -p "$ABI" "$IMPL" "$BIN"

echo "== 0. generator drift =="
python3 tools/nexcore_gen.py --spec "$SRC/units.json" --src "$SRC" --check

echo "== 1. Pass 1: publish header (only NexCore.h.ct) =="
ct build --emit-c --ns-root "$SRC" "$SRC/NexCore.h.ct" -o "$ABI"
if [ ! -s "$ABI/NexCore.h" ]; then
  echo "①a FAIL: 发布头未产出或为空: $ABI/NexCore.h" >&2
  exit 1
fi
echo "①a publish header: PASS (exit 0, non-empty)"

echo "== 1b/1c. header struct model (shared normalization) =="
python3 - "$ABI/NexCore.h" "$ABI/.forward-decls.txt" <<'PY'
import pathlib
import re
import sys


def normalize(text: str) -> str:
    """先剔字符串/字符字面量，再剔注释；两者共用一次扫描，避免顺序陷阱。"""
    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if ch == "/" and nxt == "*":
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] == "\n":
                    out.append("\n")
                i += 1
            i += 2
            continue
        if ch in ("'", '"'):
            quote = ch
            out.append(" ")  # 字面量整体抹成空格，保留词边界
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                if text[i] == "\n":
                    out.append("\n")
                i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


raw = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
norm = normalize(raw)
decls = sorted(set(re.findall(r"\bstruct\s+([A-Za-z_]\w*)\s*;", norm)))
defs = sorted(set(re.findall(r"\bstruct\s+([A-Za-z_]\w*)\s*\{", norm)))

pathlib.Path(sys.argv[2]).write_text(
    "".join(f"struct {name};\n" for name in decls), encoding="utf-8"
)

names = sorted(set(decls) | set(defs))
other = [name for name in names if name != "NexCoreConfig"]
if other:
    print(f"①b FAIL: 发布头含内部 struct 名: {other}", file=sys.stderr)
    sys.exit(1)
print(f"①b publish header structs: {names or ['(none)']} : PASS")
PY

echo "== 1c. forward-decl golden =="
diff -u abi/expected-forward-decls.txt "$ABI/.forward-decls.txt"
echo "①c forward-decls golden: PASS"

echo "== 2. Pass 2: implementation =="
# 实现单元清单：既作为构建输入，也作为 ②a 的产物清单，只维护这一份。
# 顺序与 design §6.1 一致：根单元首位。
IMPL_UNITS=(NexCore queue/queue js_host/js_host protocol/protocol)
ct build --emit-c --ns-root "$SRC" -I "$SRC" \
  $(for u in "${IMPL_UNITS[@]}"; do printf '%s ' "$SRC/$u.ct"; done) \
  -o "$IMPL"
echo "②a implementation: PASS (exit 0)"

# 产物齐全断言：任一实现单元静默没产出 / 产出空文件，都必须 FAIL。
for u in "${IMPL_UNITS[@]}"; do
  out="$IMPL/$(basename "$u").c"
  if [ ! -s "$out" ]; then
    echo "②a FAIL: 实现单元未产出非空 .c: $SRC/$u.ct -> $out" >&2
    exit 1
  fi
done
echo "②a implementation artifacts: PASS (${#IMPL_UNITS[@]}/${#IMPL_UNITS[@]} non-empty .c)"

echo "== 2b. strict cc per implementation .c =="
for c in "$IMPL"/*.c; do
  "$CC" "${CFLAGS[@]}" \
    -I "$ABI" -I "$IMPL" -I "$SRC/js_host" -I "$SRC/queue" -I "$SRC/protocol" \
    -isystem "$QJS" \
    -fsyntax-only "$c"
done
echo "②b cc -Werror=implicit-function-declaration: PASS ($(ls "$IMPL"/*.c | wc -l | tr -d ' ') files)"

echo "== 2c. exported ABI symbols (nm, all 7) =="
mkdir -p "$IMPL/obj"
for c in "$IMPL"/*.c; do
  "$CC" "${CFLAGS[@]}" \
    -I "$ABI" -I "$IMPL" -I "$SRC/js_host" -I "$SRC/queue" -I "$SRC/protocol" \
    -isystem "$QJS" \
    -c "$c" -o "$IMPL/obj/$(basename "${c%.c}").o"
done
# Mach-O 的 C 符号带前导下划线：实际输出是 `T _nexcore_create`，所以用 `_?`。
ABI_SYMS=(nexcore_create nexcore_set_sink nexcore_send nexcore_drain
          nexcore_shutdown nexcore_free nexcore_stat)
nm -g --defined-only "$IMPL/obj"/*.o > "$IMPL/nm.txt"
missing=()
for sym in "${ABI_SYMS[@]}"; do
  if ! grep -Eq " T _?${sym}$" "$IMPL/nm.txt"; then
    missing+=("$sym")
  fi
done
if [ "${#missing[@]}" -ne 0 ]; then
  echo "②c FAIL: 以下 ABI 函数不是对外可见的 T 符号（static/未导出/名字漂移）: ${missing[*]}" >&2
  echo "--- nm -g --defined-only 中的 nexcore_* ---" >&2
  grep -E 'nexcore_' "$IMPL/nm.txt" >&2 || true
  exit 1
fi
echo "②c exported ABI symbols: PASS (7/7, T; Mach-O 前导下划线已兼容)"

echo "== 3. no '::' in generated C =="
python3 tools/scan_c.py "$IMPL"

echo "== 4. recipe callee == symbol-table cname (inside generator) =="
python3 tools/nexcore_gen.py --spec "$SRC/units.json" --src "$SRC" --verify-ct "$IMPL"

echo "== 5a. unit imports: mandatory self import + whitelist (map included) =="
python3 - "$SRC" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1]).resolve()
bad = []
checked = 0

for path in sorted(root.rglob("*.ct")):
    if path.name.endswith(".h.ct"):
        continue  # 头单元没有实现体，不参与
    checked += 1
    stem = path.name[:-3]  # foo.ct -> foo
    if path.name.endswith(".m.ct"):
        stem = path.name[: -len(".m.ct")]  # queue_map.m.ct -> queue_map
    # 自身 .h.ct：只可能是同目录同 stem（映射单元的 <ns>_map.h.ct 不存在也照此判定）
    self_header = (path.parent / (stem + ".h.ct")).resolve()
    imports = []
    namespace_line = None
    for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if namespace_line is None and re.match(r"\s*namespace\s+", line):
            namespace_line = i
        m = re.match(r"\s*import\s+([^;]+);", line)
        if m:
            imports.append((i, m.group(1).strip()))

    # 只有 `.m.ct` 才是映射单元；`foo_map.ct` 仍是普通实现单元。
    is_map = path.name.endswith(".m.ct")
    if not is_map:
        first = imports[0][1] if imports else None
        if first != stem:
            bad.append(
                f"{path}: 实现单元第一条 import 必须是 `import {stem};`"
                f"（当前: {first!r}；缺了 CT 静默 exit 0 但丢命名空间前缀）"
            )

    for i, target in imports:
        if namespace_line is not None and i > namespace_line:
            bad.append(f"{path}:{i}: import 必须出现在 namespace 之前")
            continue
        if "::" in target:
            parts = target.split("::")
            if len(parts) != 2 or not parts[1].endswith("_map"):
                bad.append(f"{path}:{i}: 只允许 <ns>::<ns>_map: {target}")
                continue
            ns = parts[1][: -len("_map")]
            if parts[0] != ns:
                bad.append(f"{path}:{i}: 映射单元必须与命名空间同名: {target}")
                continue
            if not (root / ns / (ns + "_map.m.ct")).exists():
                bad.append(f"{path}:{i}: 映射单元不存在: {target}")
            continue
        # 单段：只允许自身头（实现单元自身 .h.ct；映射单元的 <ns>_map.h.ct 同理）
        if target != stem:
            bad.append(
                f"{path}:{i}: 单段 import 只允许自身 `{stem}`，"
                f"不得 import 其他单元（含映射单元）: {target}"
            )
            continue
        if not self_header.exists():
            bad.append(f"{path}:{i}: 自身头不存在: {self_header}")

if bad:
    for item in bad:
        print(item, file=sys.stderr)
    print(f"⑤a FAIL: {len(bad)} 处", file=sys.stderr)
    sys.exit(1)
print(
    f"⑤a unit imports: PASS ({checked} files; 实现单元强制 self import + "
    f"只允许自身 .h.ct / <ns>::<ns>_map；映射单元按 .m.ct 识别)"
)
PY

echo "== 5b. units.json <-> .h.ct pub functions parity =="
python3 - "$SRC" <<'PY'
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
spec = json.loads((root / "units.json").read_text(encoding="utf-8"))
decl = re.compile(r"^\s*pub\s+(?!typedef\b)[^;(){}]*?([A-Za-z_]\w*)\s*\(", re.M)
bad = []
checked = 0

for unit in spec["units"]:
    path = root / unit["source"]
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    pubs = set(decl.findall(text))
    methods = set(unit["methods"])
    checked += 1
    missing = sorted(methods - pubs)
    extra = sorted(pubs - methods)
    if missing:
        bad.append(f"{path}: units.json 声明但 .h.ct 无 pub 函数: {missing}")
    if extra:
        bad.append(f"{path}: .h.ct 的 pub 函数未列入 units.json（会静默漏掉映射）: {extra}")

if bad:
    for item in bad:
        print(item, file=sys.stderr)
    print(f"⑤b FAIL: {len(bad)} 处", file=sys.stderr)
    sys.exit(1)
print(f"⑤b units.json parity: PASS ({checked} units)")
PY

echo "== 6. stat key contract =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_host" -I "$SRC/queue" -I "$SRC/protocol" -isystem "$QJS" \
  tests/stat_keys.c "$IMPL"/*.c "$QJS/libquickjs.a" -lm -lpthread -o "$BIN/stat_keys"
"$BIN/stat_keys"
echo "⑥ stat key contract: PASS"

echo "== 7. queue FIFO =="
"$CC" "${CFLAGS[@]}" -I "$IMPL" \
  tests/queue_fifo.c "$IMPL/queue.c" -o "$BIN/queue_fifo"
"$BIN/queue_fifo"
echo "⑦ queue FIFO: PASS"

echo "== 8. bootstrap_js executed =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_host" -I "$SRC/queue" -I "$SRC/protocol" -isystem "$QJS" \
  tests/bootstrap.c "$IMPL"/*.c "$QJS/libquickjs.a" -lm -lpthread -o "$BIN/bootstrap"
"$BIN/bootstrap"
echo "⑧ bootstrap_js executed: PASS"

echo "== 9. drain -> sink end to end =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_host" -I "$SRC/queue" -I "$SRC/protocol" -isystem "$QJS" \
  tests/drain_e2e.c "$IMPL"/*.c "$QJS/libquickjs.a" -lm -lpthread -o "$BIN/drain_e2e"
"$BIN/drain_e2e"
echo "⑨ drain -> sink: PASS"

echo "== smoke: cross-unit call chain =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_host" -I "$SRC/queue" -I "$SRC/protocol" -isystem "$QJS" \
  tests/smoke.c "$IMPL"/*.c "$QJS/libquickjs.a" -lm -lpthread -o "$BIN/smoke"
"$BIN/smoke"

echo "ALL CHECKS PASSED"

# ---------------------------------------------------------------------------
# 检查项编号表（新增项追加，不复用旧号）
#   ⓪  generator drift：units.json 与 *_{map}.m.ct 生成物一致
#   ①a Pass 1 发布头：只构建 NexCore.h.ct，exit 0 且头非空
#   ①b 发布头洁净：剥字符串/注释后，除 struct NexCoreConfig 外无内部 struct 名
#   ①c 前置声明 golden：与 ①b 同一套归一化结果逐行一致
#   ②a Pass 2 实现：只列实现 .ct，exit 0，且每个单元的 .c 存在非空
#   ②b 每个实现 .c 过 cc -Werror（含 implicit-function-declaration）
#   ②c ABI 导出符号：7 个 nexcore_* 均为 nm -g --defined-only 的 T 符号
#   ③  生成 C 去掉注释/字符串后不含 '::'
#   ④  recipe callee == 符号表 cname（在生成器 AST 内判定）
#   ⑤a 单元 import：实现单元强制 self import，只允许自身 .h.ct / <ns>::<ns>_map
#   ⑤b units.json 与各 .h.ct 的 pub 函数集合一致
#   ⑥  统计键契约：10..14 → NX_E_INTERNAL 且不写 *out；0 / >=19 → NX_E_INVALID_ARGUMENT
#   ⑦  队列 FIFO：push 顺序 == pop 顺序，空队列返回 -4，深度同步递减
#   ⑧  bootstrap_js 真执行：bootstrap 定义 __ok，消息 "__ok" 经 QuickJS 求值后 sink 收到 42
#   ⑨  drain 端到端：send → queue → QuickJS → outbox → sink，顺序/深度/关闭排空
#   smoke 跨单元调用链：create → send → QuickJS → drain → sink，stat/shutdown/free
# ---------------------------------------------------------------------------
