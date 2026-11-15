#!/usr/bin/env bash
# tests/hosts_boundary_scan.sh — PH-10 E6：hosts/ 必须被边界扫描覆盖。
#
# 做法：往 hosts/shell/ 注入一个含 #if 的探针文件，默认调用（不传目标）
# check-boundaries.sh 必须返回 1 并点名该文件；跑完由 trap 删除探针。
# 判据不依赖 hosts/ 里已有真实代码，也不留下违规文件。
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
probe="$ROOT/hosts/shell/__boundary_probe.ct"

mkdir -p "$(dirname "$probe")"
printf '#if 1\nint x;\n#endif\n' > "$probe"
trap 'rm -f "$probe"' EXIT

out="$(bash "$ROOT/tools/check-boundaries.sh" 2>&1)"
rc=$?

if [ "$rc" -eq 0 ]; then
  echo "FAIL: check-boundaries.sh 默认范围没扫到 hosts/（rc=0）"
  echo "$out" | tail -3
  exit 1
fi
if ! printf '%s' "$out" | grep -q "__boundary_probe.ct"; then
  echo "FAIL: 返回非 0 但报错里没有 hosts/shell/__boundary_probe.ct"
  printf '%s\n' "$out" | tail -5
  exit 1
fi

echo "HOSTS SCAN OK"
exit 0
