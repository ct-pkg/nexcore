#!/usr/bin/env bash
# 边界规则机械校验（docs/roadmap.md:56 要求的 check-boundaries.sh）
#
# 扫描所有 .ct 源码，检查三条 P1 边界规则：
#   1. 不得出现 #if / #ifdef / #ifndef / #elif（CT 无预处理条件编译）
#   2. 不得出现 errno（错误必须走显式返回码）
#   3. import 必须集中在文件头部：任何非注释、非 #include 的声明之前
#
# 用法：bash tools/check-boundaries.sh [目录...]
#   默认扫描 src、experiments/src 与 hosts（宿主实现目录，PH-10 起纳入）
# 退出码：0 = 全过；1 = 有违规；2 = 用法或环境错误
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGETS=("$@")
if [ ${#TARGETS[@]} -eq 0 ]; then
  TARGETS=("$ROOT/src" "$ROOT/experiments/src" "$ROOT/hosts")
fi

violations=0
scanned=0
TMP_COUNT="$(mktemp)"
trap 'rm -f "$TMP_COUNT"' EXIT

for dir in "${TARGETS[@]}"; do
  [ -d "$dir" ] || continue
  while IFS= read -r f; do
    scanned=$((scanned + 1))
    rel="${f#"$ROOT"/}"

    # 1. 条件编译
    grep -nE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)([[:space:]]|$)' "$f" |
      while IFS=: read -r ln text; do
        printf '%s:%s: 禁止条件编译：%s\n' "$rel" "$ln" "$(printf '%s' "$text" | sed 's/^[[:space:]]*//')" >&2
        echo x >>"$TMP_COUNT"
      done || true

    # 2. errno
    grep -nE '\berrno\b' "$f" |
      while IFS=: read -r ln text; do
        printf '%s:%s: 禁止使用 errno：%s\n' "$rel" "$ln" "$(printf '%s' "$text" | sed 's/^[[:space:]]*//')" >&2
        echo x >>"$TMP_COUNT"
      done || true

    # 3. import 顺序：import 之后不得再出现声明
    awk -v file="$rel" '
      function strip(s) { sub(/^[[:space:]]+/, "", s); return s }
      {
        line = strip($0)
        if (line == "" || line ~ /^\/\// || line ~ /^\/\*/ || line ~ /^\*/) next
        if (line ~ /^#include/) next
        if (line ~ /^import /) { if (declared) { printf "%s:%d: import 出现在声明之后\n", file, NR; bad++ } ; next }
        if (line ~ /^(namespace|pub |struct |enum |typedef |fn |[A-Za-z_][A-Za-z0-9_]*[[:space:]*]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\()/) declared = 1
        else declared = 1
      }
      END { exit (bad > 0 ? 1 : 0) }
    ' "$f" || echo x >>"$TMP_COUNT"
  done < <(find "$dir" -name '*.ct' -type f | sort)

  # 4. hosts/ 下的 C 宿主：只许系统头与公开 ABI 头（PH-10 E6）。
  case "$dir" in
    */hosts)
      while IFS= read -r f; do
        scanned=$((scanned + 1))
        rel="${f#"$ROOT"/}"
        grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*"' "$f" |
          while IFS=: read -r ln text; do
            inc="$(printf '%s' "$text" | sed -E 's/.*"([^"]*)".*/\1/')"
            case "$inc" in
              NexCore.h) : ;;
              *)
                printf '%s:%s: hosts 只许 include 公开 ABI 头，禁止：%s\n' \
                  "$rel" "$ln" "$inc" >&2
                echo x >>"$TMP_COUNT"
                ;;
            esac
          done || true
      done < <(find "$dir" \( -name '*.c' -o -name '*.h' \) -type f | sort)
      ;;
  esac
done

count=$(wc -l <"$TMP_COUNT" | tr -d ' ')
if [ "$count" -gt 0 ]; then
  printf 'boundaries: FAIL（%s 处违规，扫描 %s 个文件）\n' "$count" "$scanned" >&2
  exit 1
fi
printf 'boundaries: PASS（%s 个文件，无 #if / 无 errno / import 顺序正确）\n' "$scanned"
