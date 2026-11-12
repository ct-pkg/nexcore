#!/usr/bin/env bash
# 逐个编译 ns_probe 用例，记录 CT 结果、C 结果与报错原文。
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/experiments/out/ns_probe"
CT="${CT:-ct}"
CC="${CC:-cc}"

rm -rf "$OUT"
mkdir -p "$OUT"

run_case() {
    local file="$1"
    local name
    name="$(basename "$file" .ct)"
    name="${name%.h}"
    local dir="$OUT/$name"
    mkdir -p "$dir"

    printf '\n===== %s (%s) =====\n' "$name" "${file#$ROOT/experiments/ns_probe/}"

    if "$CT" build --emit-c --ns-root "$HERE" -I "$HERE" "$file" -o "$dir" \
            >"$dir/build.log" 2>&1; then
        printf 'ct: OK\n'
        local ok=1 c
        for c in "$dir"/*.c; do
            "$CC" -w -c "$c" -o "${c%.c}.o" >>"$dir/build.log" 2>&1 || ok=0
        done
        if [ "$ok" -eq 1 ]; then
            printf 'cc: OK (objects)\n'
        else
            printf 'cc: FAIL\n'
            grep -E 'error:' "$dir/build.log" | head -3
        fi
        # 只打印主用例生成的 C，便于核对限定名被改写成了什么
        printf -- '--- generated ---\n'
        sed -n '1,40p' "$dir/$name.c"
    else
        printf 'ct: FAIL\n'
        grep -E 'error:|ctc:' "$dir/build.log" | head -5
    fi
}

while IFS= read -r f; do
    run_case "$f"
done < <(find "$HERE" -name '*.ct' ! -name 'mod.h.ct' ! -name 'math.h.ct' | sort)
