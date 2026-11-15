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
# 检查项：0 / ①a ①b ①c / ②a ②b ②c / ③ / ④ / ⑤a ⑤b ⑤c / ⑥ / ⑦ / ⑧ / ⑨ / smoke / ⑩
#         / ⑪ ⑫ ⑬ ⑭（P3：ABI 布局 golden、A-04 负例、schema hash、生成器）
#
# 用法：
#   bash tools/check.sh        全量检查（上面的检查项）
#   bash tools/check.sh g0     规约纳管门禁（AGENTS 6.4 / 11.5），只读，不构建
#   bash tools/check.sh g1..   未实现，退出码 2（不能跑的门禁不许装绿）
#
# 环境变量：
#   NEXCORE_JS=null      （默认）P1 档：空后端，不链接引擎；⑧⑨/smoke 记 SKIP，不计通过
#   NEXCORE_JS=quickjs   P2 档：同目录第二实现 js_backend_quickjs.ct，⑧⑨/smoke 真跑
# ---------------------------------------------------------------------------
set -euo pipefail

cd "$(dirname "$0")/.."

SRC=src
ABI=build/abi
IMPL=build/impl
BIN=build/bin
QJS=experiments/third_party/quickjs
# JS 后端档位（D-21 / P2 卡）：
#   null    —— P1 出口用；实现单元是 src/js_backend/js_backend.ct（空实现），不链接引擎。
#   quickjs —— P2 档；同一目录同一 namespace 的第二实现 js_backend_quickjs.ct，
#              链接 experiments/third_party/quickjs。
# CT 约束（实测，勿改方案）：namespace 必须等于目录名；实现单元首段 import 必须是
# 自己的 .h.ct。所以两档只能靠「同目录、换实现文件」切换，不能换目录、不能条件编译。
JS_BACKEND="${NEXCORE_JS:-null}"
case "$JS_BACKEND" in
  null)
    BACKEND_UNIT="js_backend/js_backend"
    QJS_INC=""
    QJS_LIB=""
    ;;
  quickjs)
    BACKEND_UNIT="js_backend/js_backend_quickjs"
    QJS_INC="-isystem $QJS"
    QJS_LIB="$QJS/libquickjs.a"
    if [ ! -f "$QJS_LIB" ]; then
      printf 'NEXCORE_JS=quickjs 需要 %s，当前不存在（先跑 experiments/build.sh）\n' "$QJS_LIB" >&2
      exit 2
    fi
    ;;
  *)
    printf 'NEXCORE_JS must be null or quickjs (got: %s)\n' "$JS_BACKEND" >&2
    exit 2
    ;;
esac
CC=${CC:-cc}
CFLAGS=(-std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration -Werror=incompatible-pointer-types)

# ---------------------------------------------------------------------------
# g0 —— 规约纳管（AGENTS 6.4 / 11.5）。只读仓库文件，不构建、不写 build/。
# ---------------------------------------------------------------------------
SPEC_BIN="${SPEC_BIN:-/Users/samxx/trae-ws/.ai-engineering/bin/sync-agents.sh}"
SPEC_COPY=docs/spec/ai-coding-workflow.md

g0_fail() { printf 'g0 FAIL: %s\n' "$1" >&2; exit 1; }

run_g0() {
  local head_line version sha actual want have rows

  [ -f AGENTS.md ] || g0_fail "缺 AGENTS.md"

  # 1. 生成块头与块尾
  head_line="$(grep -m1 '^<!-- spec:begin ' AGENTS.md || true)"
  [ -n "$head_line" ] || g0_fail "AGENTS.md 缺生成块头（<!-- spec:begin … -->）"
  grep -q '^<!-- spec:end -->' AGENTS.md || g0_fail "AGENTS.md 缺块尾 <!-- spec:end -->"

  version="$(printf '%s\n' "$head_line" | sed -n 's/.*version=\([0-9][0-9.]*\).*/\1/p')"
  sha="$(printf '%s\n' "$head_line" | sed -n 's/.*sha256=\([0-9a-f]\{64\}\).*/\1/p')"
  [ -n "$version" ] || g0_fail "块头缺 version="
  [ -n "$sha" ] || g0_fail "块头缺 sha256="

  # 2. 全文副本存在且 sha256 等于块头
  [ -f "$SPEC_COPY" ] || g0_fail "缺全文副本 $SPEC_COPY"
  actual="$(shasum -a 256 "$SPEC_COPY" | awk '{print $1}')"
  [ "$actual" = "$sha" ] || g0_fail "全文副本 sha256 与块头不等（块头=${sha} 实算=${actual}）"

  # 3. 块体与 sync-agents.sh --print 逐字相同；工具缺失时告警但不冒充通过
  if [ -f "$SPEC_BIN" ]; then
    want="$(mktemp)"
    have="$(mktemp)"
    bash "$SPEC_BIN" --print >"$want"
    awk '/^<!-- spec:begin/ { inb = 1 } inb { print } inb && /^<!-- spec:end/ { exit }' AGENTS.md >"$have"
    if ! cmp -s "$want" "$have"; then
      diff -u "$want" "$have" >&2 || true
      rm -f "$want" "$have"
      g0_fail "块体与 sync-agents.sh --print 不逐字相同（块被手改或未同步）"
    fi
    rm -f "$want" "$have"
  else
    printf 'g0 WARN: 未找到 %s，跳过「块体逐字」一项\n' "$SPEC_BIN" >&2
  fi

  # 4. 块外三段（块内同名小节不算）
  for h in '### 0.1 本项目实例化' '### 0.2 话术路由' '### 0.3 偏离项'; do
    grep -qxF "$h" AGENTS.md || g0_fail "缺块外小节：$h"
  done

  # 5. 实例化表 8 行（表头 + 分隔线 + 8 行 = 10）
  rows="$(awk '
      /^### 0\.1 / { inb = 1; next }
      /^### 0\.2 / { inb = 0 }
      inb && /^\|/ { n++ }
      END { print n + 0 }
  ' AGENTS.md)"
  [ "$rows" -ge 10 ] || g0_fail "实例化表只有 $((rows - 2)) 行，需 8 行"

  # 6. 版本落点非空，且落点文件里确有状态行
  grep -q '^| 版本落点 | [^|]' AGENTS.md || g0_fail "实例化表「版本落点」为空"
  grep -q '^- 状态：v[0-9]' docs/README.md || g0_fail "docs/README.md 缺「- 状态：v<版本>」行（版本落点落空）"

  # 7. 会话地图
  [ -f docs/session-map.md ] || g0_fail "缺 docs/session-map.md"

  printf 'ok g0 (block %s/%s, landing 3 段, 8 行, session-map, 版本落点)\n' "$version" "${sha:0:8}"
}

case "${1:-}" in
"" | all) ;;
g0)
  run_g0
  exit 0
  ;;
*)
  printf 'usage: bash tools/check.sh [g0]（g1..g9 未实现）\n' >&2
  exit 2
  ;;
esac

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
IMPL_UNITS=(NexCore queue/queue "$BACKEND_UNIT" protocol/protocol)
# 接口头必须独立生成（先于实现单元，避免覆盖实现产物）：quickjs 档的实现文件
# 只带自己的 .h.ct，不会产出 js_backend.h，而 NexCore.c 里 import 的是 js_backend::js_backend_map。
ct build --emit-c --ns-root "$SRC" -I "$SRC" "$SRC/js_backend/js_backend.h.ct" -o "$IMPL" >/dev/null
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
    -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" \
    $QJS_INC \
    -fsyntax-only "$c"
done
echo "②b cc -Werror=implicit-function-declaration: PASS ($(ls "$IMPL"/*.c | wc -l | tr -d ' ') files)"

echo "== 2c. exported ABI symbols (nm, all 7) =="
mkdir -p "$IMPL/obj"
for c in "$IMPL"/*.c; do
  "$CC" "${CFLAGS[@]}" \
    -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" \
    $QJS_INC \
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

echo "== ⑤c boundaries (tools/check-boundaries.sh) =="
bash tools/check-boundaries.sh "$SRC" experiments/src hosts

echo "== 6. stat key contract =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/stat_keys.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/stat_keys"
"$BIN/stat_keys"
echo "⑥ stat key contract: PASS"

echo "== 7. queue FIFO =="
"$CC" "${CFLAGS[@]}" -I "$IMPL" \
  tests/queue_fifo.c "$IMPL/queue.c" -o "$BIN/queue_fifo"
"$BIN/queue_fifo"
echo "⑦ queue FIFO: PASS"

if [ "$JS_BACKEND" = "quickjs" ]; then
echo "== 8. bootstrap_js executed =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/bootstrap.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/bootstrap"
"$BIN/bootstrap"
echo "⑧ bootstrap_js executed: PASS"

echo "== 9. drain -> sink end to end =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/drain_e2e.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/drain_e2e"
"$BIN/drain_e2e"
echo "⑨ drain -> sink: PASS"

echo "== smoke: cross-unit call chain =="
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/smoke.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/smoke"
"$BIN/smoke"
else
echo "⑧ bootstrap_js executed: SKIP (NEXCORE_JS=null：空后端不求值，不计通过)"
echo "⑨ drain -> sink: SKIP (NEXCORE_JS=null：空后端不产出响应，不计通过)"
echo "smoke: SKIP (NEXCORE_JS=null：端到端依赖 JS 求值，不计通过)"
echo "   JS 语义检查（⑧⑨/smoke）在 null 档不覆盖（D-21）；quickjs 档真跑"
fi

echo "== 10. libnexcore 产物面（依赖 + 符号） =="
# 只导出 ABI 的 7 个函数；内部单元符号一律隐藏（ACC-A-02 白名单）
cat >"$BIN/exports.txt" <<'SYMS'
_nexcore_create
_nexcore_set_sink
_nexcore_send
_nexcore_drain
_nexcore_shutdown
_nexcore_stat
_nexcore_free
SYMS
"$CC" "${CFLAGS[@]}" -dynamiclib \
  -Wl,-exported_symbols_list,"$BIN/exports.txt" \
  -o "$BIN/libnexcore.dylib" "$IMPL"/obj/*.o $QJS_LIB -lm -lpthread

# 静态归档（PH-10 E5）：只收本档实现对象；quickjs 档的引擎不进归档，
# 由消费者自行链接（与 dylib 档链接口径一致；⑰ 会核对对象集合与链接结果）。
rm -f "$BIN/libnexcore.a"
ar rcs "$BIN/libnexcore.a" "$IMPL"/obj/*.o
echo "ar t libnexcore.a:"
ar t "$BIN/libnexcore.a"
echo "otool -L:"
otool -L "$BIN/libnexcore.dylib"
if otool -L "$BIN/libnexcore.dylib" | grep -qi 'quickjs'; then
  printf '⑩ FAIL: libnexcore 依赖里出现 QuickJS\n' >&2
  exit 1
fi
syms=$(nm -gU "$BIN/libnexcore.dylib" | awk '$2 == "T" { print $3 }' | sort)
echo "nm -gU T:"
printf '%s\n' "$syms"
count=$(printf '%s\n' "$syms" | grep -c . || true)
if [ "$count" -ne 7 ]; then
  printf '⑩ FAIL: libnexcore 导出 T 符号应为 7 个，实际 %s 个\n' "$count" >&2
  exit 1
fi
if printf '%s\n' "$syms" | grep -v '^_nexcore_' >/dev/null; then
  printf '⑩ FAIL: libnexcore 出现非 nexcore_* 的导出符号\n' >&2
  exit 1
fi
echo "⑩ libnexcore: PASS（仅 libc/pthread，7 个 nexcore_* 导出）"

echo "== 11. ABI 布局 golden（ACC-A-10） =="
# 基线来自实测：abi/abi-layout.txt 只能由 tools/abi_layout.py --write 用真实编译器产出。
python3 tools/abi_layout.py --header "$ABI/NexCore.h" --golden abi/abi-layout.txt \
  --emit "$ABI/abi-layout.txt" --check
diff -u abi/abi-layout.txt "$ABI/abi-layout.txt"
echo "⑪ ABI layout golden: PASS（$ABI/abi-layout.txt == abi/abi-layout.txt）"

echo "== 12. ABI 冻结负例 + struct_size 协商（ACC-A-04） =="
# 负例 A：插入字段但不升 abi_version —— golden 必须 diff。
# 负例 B：两个同宽度字段互换 —— offset/size 都不变，只有 order= 行能拦，专门验证“重排可检”。
mkdir -p "$ABI/mut/insert" "$ABI/mut/reorder"
python3 - "$ABI/NexCore.h" "$ABI/mut/insert/NexCore.h" "$ABI/mut/reorder/NexCore.h" <<'PY'
import pathlib
import sys

src = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
insert = src.replace(
    "    uint32_t reserved;\n",
    "    uint32_t reserved;\n    uint32_t abi_probe_extra;\n",
)
assert insert != src, "insert mutation did not apply"
reorder = src.replace(
    "    uint32_t flags;\n    uint32_t reserved;\n",
    "    uint32_t reserved;\n    uint32_t flags;\n",
)
assert reorder != src, "reorder mutation did not apply"
for target, text in ((sys.argv[2], insert), (sys.argv[3], reorder)):
    path = pathlib.Path(target)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
PY
for m in insert reorder; do
  rc=0
  python3 tools/abi_layout.py --header "$ABI/mut/$m/NexCore.h" --check \
    >/dev/null 2>"$ABI/mut/$m.diff" || rc=$?
  if [ "$rc" -ne 2 ]; then
    echo "⑫ FAIL: 负例 $m 未被布局 golden 拦下（rc=$rc，期望 2）" >&2
    cat "$ABI/mut/$m.diff" >&2
    exit 1
  fi
done
grep -q 'abi_probe_extra' "$ABI/mut/insert.diff" || {
  echo "⑫ FAIL: 插入字段的 diff 里没有 abi_probe_extra" >&2
  exit 1
}
grep -q 'order=.*reserved,flags' "$ABI/mut/reorder.diff" || {
  echo "⑫ FAIL: 重排负例没有体现在 order= 行" >&2
  exit 1
}
echo "⑫ layout drift negatives: PASS（插入 / 同宽度重排都必须 diff）"

"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/config_negotiate.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/config_negotiate"
"$BIN/config_negotiate"
echo "⑫ struct_size 协商: PASS（<MIN / reserved!=0 / 版本不符拒绝；==MIN 与 >CURRENT 接受）"

echo "== 13. Schema hash 三方一致 + 不匹配负例（ACC-A-11） =="
python3 tools/schema_hash.py --check \
  --schema tests/fixture.schema.yaml --golden abi/schema-hash.txt --c-source src/NexCore.ct
python3 tools/schema_hash.py --emit-header "$ABI/schema_hash_test.h" \
  --schema tests/fixture.schema.yaml
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/schema_mismatch.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/schema_mismatch"
"$BIN/schema_mismatch"
echo "⑬ schema hash: PASS（schema/golden/C 常量一致；错 hash 拒绝且重试成功）"

echo "== 14. P3 生成器（Schema -> 四类产物） =="
# 依赖 PyYAML：缺了必须 rc=2，不许装绿。
if ! python3 -c 'import yaml' >/dev/null 2>&1; then
  echo "⑭ UNVERIFIED: 缺 PyYAML（python3 -m pip install --user 'pyyaml==6.0.1'），生成器门禁不可判定" >&2
  exit 2
fi
python3 tools/nexcore_schema_gen.py --schema tests/fixture.schema.yaml \
  --out schema/generated --check
python3 tools/nexcore_schema_gen.py --negatives tests/gen_negatives \
  --expect tests/gen_negatives/expected.tsv
"$CC" "${CFLAGS[@]}" -I "$ABI" -I schema/generated/c \
  -fsyntax-only schema/generated/c/nexcore_registry.c

# 行为断言（GEN-05/12）：生成物不是文本摆件 —— 编译成可执行，让 dispatch 真走到 provider。
"$CC" "${CFLAGS[@]}" -I "$ABI" -I schema/generated/c \
  schema/generated/c/nexcore_registry.c tests/gen_registry_behavior.c \
  -o "$BIN/gen_registry_behavior"
"$BIN/gen_registry_behavior"
echo "⑭ registry 行为: PASS（wrapper/adaptor/dispatch 三层都真调 provider，单次派发）"

# GEN-16 机制：跨单元取址必须由编译期拒绝，不靠 grep。
mkdir -p "$IMPL/gen_neg"
python3 - "$IMPL/gen_neg/nexcore_registry_bad.c" <<'PY'
import pathlib
import sys

src = pathlib.Path("schema/generated/c/nexcore_registry.c").read_text(encoding="utf-8")
pathlib.Path(sys.argv[1]).write_text(
    src + "\nvoid *nx_gen_bad_addr(void) { return (void *)&queue_push; }\n",
    encoding="utf-8",
)
PY
rc=0
"$CC" "${CFLAGS[@]}" -I "$ABI" -I schema/generated/c -fsyntax-only \
  "$IMPL/gen_neg/nexcore_registry_bad.c" 2>"$IMPL/gen_neg/bad.err" || rc=$?
if [ "$rc" -eq 0 ]; then
  echo "⑭ FAIL: 跨单元取址 &queue_push 本应编译失败（GEN-16）" >&2
  exit 1
fi
if ! grep -q 'queue_push' "$IMPL/gen_neg/bad.err"; then
  echo "⑭ FAIL: 负例失败原因不是 queue_push 未声明（GEN-16 判据不成立）" >&2
  cat "$IMPL/gen_neg/bad.err" >&2
  exit 1
fi
echo "⑭ GEN-16 跨单元取址: PASS（编译期拒绝，错误信息指向 queue_push）"
if grep -nE '#define[[:space:]]+[A-Za-z_]+\(' schema/generated/c/*; then
  echo "⑭ FAIL: 生成 C 出现函数式宏（GEN-03）" >&2
  exit 1
fi
python3 tools/scan_c.py schema/generated/c
if grep -rnE '\b(eval|Proxy|dlsym|Mirror|kotlin-reflect|JSONSerialization|new Function)\b' \
    schema/generated; then
  echo "⑭ FAIL: 生成物出现禁止符号（GEN-04）" >&2
  exit 1
fi
if grep -rnE '\b(Any|dynamic)\b|JsonElement' schema/generated/swift schema/generated/kotlin; then
  echo "⑭ FAIL: 公开 API 出现 Any/dynamic/JsonElement（GEN-07）" >&2
  exit 1
fi
if command -v swiftc >/dev/null 2>&1; then
  swiftc -typecheck schema/generated/swift/*.swift
  echo "⑭ Swift typecheck: PASS（swiftc -typecheck）"
else
  echo "⑭ Swift typecheck: UNVERIFIED（本机无 swiftc，不计通过）"
fi
echo "⑭ Kotlin compile: UNVERIFIED（本机无 kotlinc；按 P3 规划标不可验证，不进绿线）"
if [ "$JS_BACKEND" = "quickjs" ]; then
  "$CC" "${CFLAGS[@]}" -I "$SRC/js_backend" $QJS_INC \
    tests/js_module_load.c $QJS_LIB -lm -o "$BIN/js_module_load"
  "$BIN/js_module_load" schema/generated/js/nexcore_fixture.js
  echo "⑭ JS module load: PASS（QuickJS 以 module 求值通过）"
else
  echo "⑭ JS module load: UNVERIFIED（NEXCORE_JS=null 不链接引擎，不计通过）"
fi
echo "⑭ P3 generator: PASS（5 产物无漂移、20/20 负例真红、生成 C 过 -Werror）"

echo "== 15. 微任务泵（PH-10 E1：宿主经 send+drain 可观察 Promise） =="
# 判据：不新增导出符号，只用 ABI 的 send/drain，Promise 回调必须已执行且链按序。
# null 档没有引擎，本项记 N/A（不是 PASS，也不计入 SKIP：D-21 把 JS 语义检查放在 quickjs 档）。
if [ "$JS_BACKEND" = "quickjs" ]; then
  "$CC" "${CFLAGS[@]}" \
    -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
    tests/pump_microtask.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/pump_microtask"
  "$BIN/pump_microtask" || { echo "⑮ FAIL: 微任务未被泵到（Promise 回调不可见）" >&2; exit 1; }
  "$CC" "${CFLAGS[@]}" \
    -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
    tests/pump_bounds.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/pump_bounds"
  "$BIN/pump_bounds" || { echo "⑮ FAIL: 泵边界（max_jobs / shutdown 收尾）不成立" >&2; exit 1; }
  echo "⑮ microtask pump: PASS（hits=1、两级链 n=2、有界、max_jobs、收尾泵）"
else
  echo "⑮ microtask pump: N/A (NEXCORE_JS=null：无引擎，不计通过)"
fi

echo "== 16. 原生 Provider 注册（PH-10 E3：注册可用 + 向后兼容） =="
# 判据：注册名可调用且 JSON 往返；未注册名与回调错误都抛错；
# 老 struct_size（==MIN）即使尾部是垃圾也不得被读（否则会当函数指针调用）。
if [ "$JS_BACKEND" = "quickjs" ]; then
  "$CC" "${CFLAGS[@]}" \
    -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
    tests/native_provider.c "$IMPL"/*.c $QJS_LIB -lm -lpthread -o "$BIN/native_provider"
  "$BIN/native_provider" || { echo "⑯ FAIL: 原生 Provider 未生效或越界读了尾字段" >&2; exit 1; }
  echo "⑯ native provider: PASS（注册/未注册名/清单外直调/回调错误/五档 struct_size）"
else
  echo "⑯ native provider: N/A (NEXCORE_JS=null：无引擎，不计通过)"
fi

echo "== 17. 静态归档（PH-10 E5：libnexcore.a 可独立链接） =="
ARCHIVE="$BIN/libnexcore.a"
if [ ! -f "$ARCHIVE" ]; then
  echo "⑰ FAIL: $ARCHIVE 不存在（构建未产出静态归档）" >&2
  exit 1
fi
want=$(cd "$IMPL" && ls *.c | sed 's/\.c$/.o/' | sort | tr '\n' ' ')
got=$(ar t "$ARCHIVE" | grep -v '^__.SYMDEF' | grep -v '^$' | sort | tr '\n' ' ')
if [ "$want" != "$got" ]; then
  echo "⑰ FAIL: 归档对象集合与 build/impl/*.c 不符" >&2
  echo "  want: $want" >&2
  echo "  got : $got" >&2
  exit 1
fi
for s in create set_sink send drain shutdown free stat; do
  nm -g --defined-only "$ARCHIVE" | grep -q "T _nexcore_$s\$" || {
    echo "⑰ FAIL: 归档缺符号 nexcore_$s" >&2
    exit 1
  }
done
"$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/smoke.c "$ARCHIVE" $QJS_LIB -lm -lpthread -o "$BIN/smoke_archive" || {
  echo "⑰ FAIL: 只链 libnexcore.a 无法链接 tests/smoke.c" >&2
  exit 1
}
extra=$(otool -L "$BIN/smoke_archive" | awk 'NR>1{print $1}' | grep -v '^/usr/lib/libSystem' | grep -v '^$' || true)
if [ -n "$extra" ]; then
  echo "⑰ FAIL: 静态归档带出了额外动态依赖：$extra" >&2
  exit 1
fi
cp "$ARCHIVE" /tmp/nx_archive_neg.a
ar d /tmp/nx_archive_neg.a NexCore.o
if "$CC" "${CFLAGS[@]}" \
  -I "$ABI" -I "$IMPL" -I "$SRC/js_backend" -I "$SRC/queue" -I "$SRC/protocol" $QJS_INC \
  tests/smoke.c /tmp/nx_archive_neg.a $QJS_LIB -lm -lpthread -o /tmp/nx_archive_neg >/dev/null 2>&1; then
  echo "⑰ FAIL: 去掉 NexCore.o 后仍能链接，判据空转" >&2
  exit 1
fi
if [ "$JS_BACKEND" = "quickjs" ]; then
  "$BIN/smoke_archive" | grep -q "SMOKE OK" || {
    echo "⑰ FAIL: 归档链接版 smoke 未跑出 SMOKE OK" >&2
    exit 1
  }
  echo "⑰ archive: PASS（对象集合 / 7 符号 / 仅 libSystem / 运行 SMOKE OK / 缺对象必红）"
else
  echo "⑰ archive: N/A-RUN (NEXCORE_JS=null：可链接，JS 语义不在本档覆盖)"
fi

echo "ALL CHECKS PASSED (NEXCORE_JS=$JS_BACKEND)"
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
#   ⑪  ABI 布局 golden：tools/abi_layout.py 用真实编译器实测 build/abi/NexCore.h，
#       与 abi/abi-layout.txt 逐行一致（含 order= 行，同宽度重排也能拦）
#   ⑫  ABI 冻结负例：插入字段 / 同宽度重排都必须产生 diff；config 协商运行时负例
#   ⑬  Schema hash：tests/fixture.schema.yaml 字节 == abi/schema-hash.txt ==
#       src/NexCore.ct 的 NEXCORE_SCHEMA_HASH；错 hash 拒绝且同进程重试成功
#   ⑭  P3 生成器：Schema -> 5 个产物无漂移、20 条负例全部真红（规则+行号）、
#       生成 C 过 -Werror；registry 行为测试（dispatch -> provider，GEN-05/12）；
#       GEN-16 跨单元取址编译期负例；Swift 用 swiftc 判，Kotlin/无引擎时的 JS 标 UNVERIFIED
# ---------------------------------------------------------------------------
