#!/usr/bin/env python3
"""检查 ③：生成的 C 里不得出现 `::`。

先剥掉注释与字符串/字符字面量，再搜索；否则注释里出现 `queue::Ring`
或字符串里出现 `::` 会误报。
"""

from __future__ import annotations

import sys
from pathlib import Path


def strip_and_find(text: str) -> list[tuple[int, int]]:
    out: list[str] = []
    i = 0
    n = len(text)
    line = 1
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
                    line += 1
                i += 1
            i += 2
            continue
        if ch == '"' or ch == "'":
            quote = ch
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
                    line += 1
                i += 1
            continue
        out.append(ch)
        if ch == "\n":
            line += 1
        i += 1

    hits: list[tuple[int, int]] = []
    for lineno, raw in enumerate("".join(out).splitlines(), 1):
        col = raw.find("::")
        if col >= 0:
            hits.append((lineno, col + 1))
    return hits


def main(argv: list[str]) -> int:
    roots = [Path(a) for a in argv[1:]] or [Path("build/impl")]
    files: list[Path] = []
    for root in roots:
        files.extend(sorted(root.rglob("*.c")) if root.is_dir() else [root])
    bad = 0
    for path in files:
        for line, col in strip_and_find(path.read_text(encoding="utf-8")):
            print(f"{path}:{line}:{col}: '::' 出现在生成的 C 中", file=sys.stderr)
            bad += 1
    if bad:
        print(f"③ FAIL: {bad} 处 '::'", file=sys.stderr)
        return 1
    print(f"③ generated C has no '::': PASS ({len(files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
