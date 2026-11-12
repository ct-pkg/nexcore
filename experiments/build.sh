#!/usr/bin/env bash
#
# experiments/build.sh — 重建并运行全部地基验证。
#
# 依赖：ct（v0.19.1 实测）、cc、make、curl、tar；Swift 可选。
# QuickJS 只在 third_party/quickjs/libquickjs.a 缺失时下载。
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
CT="${CT:-ct}"
CC="${CC:-cc}"
QJS="$HERE/third_party/quickjs"
QJS_VERSION="${QJS_VERSION:-2025-09-13}"

log()  { printf '\n== %s ==\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
pass() { printf 'PASS: %s\n' "$*"; }

command -v "$CT" >/dev/null 2>&1 || fail "ct not found in PATH (set CT=...)"
command -v "$CC" >/dev/null 2>&1 || fail "C compiler not found (set CC=...)"

mkdir -p "$OUT"

# ---------------------------------------------------------------- QuickJS
if [ ! -f "$QJS/libquickjs.a" ]; then
    log "fetch + build QuickJS $QJS_VERSION"
    if [ ! -d "$QJS" ]; then
        tmp="$(mktemp -d)"
        trap 'rm -rf "$tmp"' EXIT
        mkdir -p "$HERE/third_party"
        curl -fsSL "https://bellard.org/quickjs/quickjs-$QJS_VERSION.tar.xz" \
            -o "$tmp/quickjs.tar.xz"
        tar xf "$tmp/quickjs.tar.xz" -C "$HERE/third_party"
        mv "$HERE/third_party/quickjs-$QJS_VERSION" "$QJS"
    fi
    make -C "$QJS" libquickjs.a -j"${JOBS:-4}"
fi

# ------------------------------------------------- 1. 同步 / 异步 / 错误
log "1. bridge.ct: sync + async + errors"
"$CT" compile "$HERE/src/bridge.ct" -o "$OUT/bridge.c"
"$CC" -Wall -Wextra -Wno-unused-parameter -I "$QJS" \
    "$OUT/bridge.c" "$QJS/libquickjs.a" -lm -lpthread -o "$OUT/bridge"
"$OUT/bridge" "$HERE/js/bridge_test.js" | tee "$OUT/bridge.run.txt"

grep -q 'sync nativeAdd(2,3)=5'          "$OUT/bridge.run.txt" || fail "sync JS->native"
grep -q 'native error caught: native refused the call' "$OUT/bridge.run.txt" || fail "native error path"
grep -q 'called JS jsMultiply(6) -> 42'  "$OUT/bridge.run.txt" || fail "CT->JS call"
grep -q 'async resolved=42'              "$OUT/bridge.run.txt" || fail "promise resolve"
grep -q 'async reject caught: worker rejected the job' "$OUT/bridge.run.txt" || fail "promise reject"
grep -q 'exception from throw: Error: js boom' "$OUT/bridge.run.txt" || fail "JS throw path"
grep -q 'clean shutdown'                 "$OUT/bridge.run.txt" || fail "clean shutdown"
pass "sync / async / all three error paths"

# ------------------------------------------------------- 2. C ABI 导出
log "2. abi.h.ct: exported C ABI (C + Swift)"
rm -rf "$OUT/abibuild"
mkdir -p "$OUT/abibuild"
# 注意：ct compile 生成的 .h 只有 include guard；导出原型必须用 ct build --emit-c。
"$CT" build --emit-c "$HERE/src/abi.h.ct" -o "$OUT/abibuild"
grep -q 'nexcore_abi_add' "$OUT/abibuild/abi.h" || fail "abi.h missing exported prototype"
grep -q 'NexcoreCallback' "$OUT/abibuild/abi.h" || fail "abi.h missing exported typedef"

"$CC" -Wall -Wextra -I "$OUT/abibuild" \
    "$HERE/host/abi_host.c" "$OUT/abibuild/abi.c" -o "$OUT/abi_host"
"$OUT/abi_host" | tee "$OUT/abi_host.txt"
grep -q 'host add=42 version=1' "$OUT/abi_host.txt" || fail "C host add"
grep -q 'host apply=42'         "$OUT/abi_host.txt" || fail "C host callback"

$CC -c -I "$OUT/abibuild" "$OUT/abibuild/abi.c" -o "$OUT/abi.o"
nm -gU "$OUT/abi.o" | grep -q '_nexcore_abi_add' || fail "symbol not exported"

if command -v swiftc >/dev/null 2>&1; then
    swiftc -import-objc-header "$OUT/abibuild/abi.h" \
        "$HERE/host/abi_host.swift" "$OUT/abi.o" -o "$OUT/abi_host_swift"
    "$OUT/abi_host_swift" | tee "$OUT/abi_host_swift.txt"
    grep -q 'swift add=42 version=1' "$OUT/abi_host_swift.txt" || fail "Swift host add"
    grep -q 'swift apply=42'         "$OUT/abi_host_swift.txt" || fail "Swift host callback"
    pass "C ABI exported; C and Swift hosts agree"
else
    pass "C ABI exported; C host agrees (swiftc not found, Swift skipped)"
fi

# ------------------------------------------------------- 3. 结构体布局
log "3. layout.ct vs layout_ref.c"
"$CT" compile "$HERE/src/layout.ct" -o "$OUT/layout.c"
"$CC" "$OUT/layout.c" -o "$OUT/layout_ct_bin"
"$CC" "$HERE/host/layout_ref.c" -o "$OUT/layout_ref_bin"
"$OUT/layout_ct_bin" > "$OUT/layout_ct.txt"
"$OUT/layout_ref_bin" > "$OUT/layout_c.txt"
diff -u "$OUT/layout_c.txt" "$OUT/layout_ct.txt" || fail "CT/C layout divergence"
cat "$OUT/layout_ct.txt"
pass "struct layout identical to C"

# ------------------------------------------------- 4. 跨单元 import
log "4. cross-unit import (ct build)"
build_unit() {
    local src="$1" dir="$2" name
    name="$(basename "$src" .ct)"
    rm -rf "$dir"
    mkdir -p "$dir"
    "$CT" build --emit-c "$src" -o "$dir"
    # shellcheck disable=SC2046
    "$CC" -Wall -Wextra $(find "$dir" -name '*.c') -o "$dir/run"
    "$dir/run"
}
build_unit "$HERE/src/crossunit_demo.h.ct" "$OUT/cross_h" | tee "$OUT/cross_h.txt"
build_unit "$HERE/src/crossunit_plain.ct"  "$OUT/cross_plain" | tee "$OUT/cross_plain.txt"
grep -q 'crossunit add=42 version=1' "$OUT/cross_h.txt" || fail "cross-unit .h.ct import"
grep -q 'plain importer add=3'       "$OUT/cross_plain.txt" || fail "cross-unit plain .ct import"
pass "import works in ct build (plain .ct importer included)"

# ------------------------------------------------------- 5. 语言缺口探针
log "5. known language gaps (expected failures + workarounds)"
gaps="$OUT/gaps"
rm -rf "$gaps"
mkdir -p "$gaps"

expect_fail() {
    local name="$1" pattern="$2"
    shift 2
    if "$@" >"$gaps/$name.log" 2>&1; then
        cat "$gaps/$name.log"
        fail "$name unexpectedly succeeded"
    fi
    grep -q "$pattern" "$gaps/$name.log" || {
        cat "$gaps/$name.log"
        fail "$name failed with an unexpected message"
    }
    printf '  %-24s -> %s\n' "$name" "$(head -1 "$gaps/$name.log")"
}

expect_fail import_in_compile_mode "'import' is not supported in compile mode" \
    "$CT" compile "$HERE/gaps/import_in_compile_mode.ct" -o "$gaps/import.c"
expect_fail errno_probe "undeclared variable" \
    "$CT" compile "$HERE/gaps/errno_probe.ct" -o "$gaps/errno.c"
expect_fail ifdef_probe "expected an expression" \
    "$CT" compile "$HERE/gaps/ifdef_probe.ct" -o "$gaps/ifdef.c"

"$CT" compile "$HERE/gaps/errno_ok.ct" -o "$gaps/errno_ok.c"
"$CC" -I "$HERE/gaps" "$gaps/errno_ok.c" -o "$gaps/errno_ok"
"$gaps/errno_ok" | grep -q 'errno=0' || fail "errno shim workaround"

"$CT" compile "$HERE/gaps/ifdef_ok.ct" -o "$gaps/ifdef_ok.c"
"$CC" -I "$HERE/gaps" "$gaps/ifdef_ok.c" -o "$gaps/ifdef_ok"
"$gaps/ifdef_ok" | grep -q 'feature=1' || fail "#ifdef shim workaround"

# import 放在 namespace 之后：不报错，但 include 被静默丢弃。
mkdir -p "$gaps/import_after_ns"
"$CT" build --emit-c --ns-root "$HERE/gaps" -I "$HERE/src" \
    "$HERE/gaps/demo/import_after_ns.h.ct" -o "$gaps/import_after_ns"
if grep -q '#include "abi.h"' "$gaps/import_after_ns/import_after_ns.c"; then
    fail "import after namespace was not dropped (gap changed)"
fi
grep -q 'nexcore_abi_add' "$gaps/import_after_ns/import_after_ns.c" \
    || fail "import-after-namespace probe produced no call"
printf '  %-24s -> import silently dropped (no #include "abi.h")\n' import_after_ns

# ns::fn 在 build 模式可正常解析（本次未复现解析歧义）。
mkdir -p "$gaps/ns_qualified"
"$CT" build --emit-c --ns-root "$HERE/gaps" \
    "$HERE/gaps/demo/ns_qualified_probe.h.ct" -o "$gaps/ns_qualified"
grep -q 'demo_helper(1)' "$gaps/ns_qualified/ns_qualified_probe.c" \
    || fail "ns::fn resolution"
printf '  %-24s -> demo::helper(1) resolved to demo_helper(1)\n' ns_qualified

mkdir -p "$gaps/ns_cross"
"$CT" build --emit-c --ns-root "$HERE/gaps" -I "$HERE/gaps" \
    "$HERE/gaps/demo/ns_cross_probe.h.ct" -o "$gaps/ns_cross"
grep -q 'other_twice(21)' "$gaps/ns_cross/ns_cross_probe.c" \
    || fail "cross-namespace ns::fn resolution"
printf '  %-24s -> other::twice(21) resolved to other_twice(21)\n' ns_cross

pass "gap probes recorded in $gaps"

log "ALL CHECKS PASSED"
