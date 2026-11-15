#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# tools/check-host.sh —— hosts/shell 的独立验收入口（PH-11）
#
# 归属：宿主用例走独立脚本，不并入 tools/check.sh 的绿线，也不占检查编号。
# 依赖：build/bin/libnexcore.a（quickjs 档构建）+ experiments/third_party/quickjs/libquickjs.a。
#       缺依赖时 rc=2 并打印缺什么——不许静默绿。
# 用法：bash tools/check-host.sh
# 退出码：0 = 全过；1 = 有用例失败；2 = 用法或环境错误
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/bin"
ARCHIVE="$BIN/libnexcore.a"
QJS="$ROOT/experiments/third_party/quickjs/libquickjs.a"
HOST_SRC="$ROOT/hosts/shell/main.c"
NEX="$BIN/nex"

[ -f "$ARCHIVE" ] || { echo "check-host: 缺 $ARCHIVE（先跑 NEXCORE_JS=quickjs bash tools/check.sh）" >&2; exit 2; }
[ -f "$QJS" ] || { echo "check-host: 缺 $QJS（先跑 experiments/build.sh）" >&2; exit 2; }
[ -f "$HOST_SRC" ] || { echo "check-host: FAIL 缺 hosts/shell/main.c" >&2; exit 1; }

cc -std=c99 -Wall -Wextra -Werror -I "$ROOT/build/abi" \
  "$HOST_SRC" "$ARCHIVE" "$QJS" -lm -lpthread -o "$NEX"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "check-host: FAIL $1" >&2; exit 1; }

# 正例 1：普通脚本（程序模式，多语句）
printf 'var a=1; a+1\n' >"$TMP/basic.nexjs"
out="$("$NEX" "$TMP/basic.nexjs")" || fail "basic 脚本退出码非 0"
[ "$out" = "2" ] || fail "basic 输出应为 2，实际：$out"

# 正例 2：shebang 直跑（内核传 argv，不依赖安装到 PATH）
printf '#!%s\nvar b=2; b+1\n' "$NEX" >"$TMP/shebang.nexjs"
chmod +x "$TMP/shebang.nexjs"
out="$("$TMP/shebang.nexjs")" || fail "shebang 脚本退出码非 0"
[ "$out" = "3" ] || fail "shebang 输出应为 3，实际：$out"

# 正例 3：Shell.print 走 PH-10 的 Provider 注册
printf 'Shell.print("hi")\n' >"$TMP/print.nexjs"
out="$("$NEX" "$TMP/print.nexjs")" || fail "Shell.print 脚本退出码非 0"
[ "$out" = "hi" ] || fail "Shell.print 输出应为 hi，实际：$out"

# 负例 1：语法错 → rc=1 且 stderr 非空
printf 'var = ;\n' >"$TMP/broken.nexjs"
set +e
err="$("$NEX" "$TMP/broken.nexjs" 2>&1 >/dev/null)"
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "语法错应 rc=1，实际 rc=$rc"
[ -n "$err" ] || fail "语法错未向 stderr 输出任何内容"

# 负例 2：缺参数 → rc=2
set +e
"$NEX" >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 2 ] || fail "缺参数应 rc=2，实际 rc=$rc"

echo "HOST OK"
