# P2 收口记录

> 日期：2026-11-15 ｜ 基线：`ct v0.44.2`（`328e5e63`）｜ 提交：`4b943fd`（接通 quickjs 档）
> 上游：`docs/reviews/nexcore-p2-card-draft.md`（卡）、`docs/reviews/nexcore-p1-closeout.md`（P1）

## 1. 两档实测

| 档位 | 末行原文 | rc | SKIP 计数 | 含义 |
| --- | --- | --- | --- | --- |
| `NEXCORE_JS=null` | `ALL CHECKS PASSED (NEXCORE_JS=null)` | 0 | **3** | ⑧⑨/smoke 仍空转、不计通过；P1 口径不变 |
| `NEXCORE_JS=quickjs` | `ALL CHECKS PASSED (NEXCORE_JS=quickjs)` | 0 | **0** | ⑧⑨/smoke 真跑；`bootstrap_js executed: PASS`、`drain -> sink: PASS`、smoke 实际执行 |

两档跑在仓库副本（`build/` 被 macOS 封存，见 `docs/toolchain.md`），QuickJS 静态库复用已构建的 `libquickjs.a`，`experiments/third_party/` 未删。

`g0` 在两档之外单独跑：`ok g0 (block 1.9.0/b8651332, landing 3 段, 8 行, session-map, 版本落点)`，rc=0。

## 2. 零删除回退的三步实测

回退手段是「给 quickjs 分支加回 `exit 2`」，不删任何文件。三步都在副本上跑：

| 步 | 操作 | 结果 |
| --- | --- | --- |
| 1 | 在 `check.sh` 的 `quickjs)` 分支首行插入 `printf "regression: quickjs disabled\n" >&2; exit 2` | `NEXCORE_JS=quickjs bash tools/check.sh` → **rc=2**，打印 `regression: quickjs disabled` |
| 2 | 同一份改动下跑 null 档 | `bash tools/check.sh` → **rc=0**，末行 `ALL CHECKS PASSED (NEXCORE_JS=null)`，⑧⑨/smoke 仍 SKIP |
| 3 | 复原 `check.sh` | quickjs 档回到 **rc=0**，末行 `ALL CHECKS PASSED (NEXCORE_JS=quickjs)` |

结论：回退是**零删除**的——实现文件留在 `src/js_backend/` 不影响 null 档（未注册的兄弟文件不会弄红 ⓪④⑤a⑤b，这条在卡 §4.1 已实测）。

## 3. ②a 生成顺序的坑

**现象**：接线后第一次跑 quickjs 档，②b 直接红：

```
build/impl/NexCore.c:4:10: fatal error: 'js_backend.h' file not found
```

**原因**：quickjs 档的实现单元是 `js_backend_quickjs.ct`，它只带自己的 `js_backend_quickjs.h.ct`，不产出 `js_backend.h`；而 `NexCore.c` 里 `import js_backend::js_backend_map` 需要 `js_backend.h`。我的第一版修法是在单元生成**之后**补一条只生成接口头的 `ct build`——结果把 `js_backend.c` 覆盖成空文件，链接时报：

```
"_js_backend_init", referenced from:
    _nexcore_create in NexCore-2f3c97.o
ld: symbol(s) not found for architecture arm64
```

**修法**：把接口头生成挪到**单元生成之前**，让随后生成的实现 `.c` 覆盖掉空壳：

```
ct build --emit-c --ns-root "$SRC" -I "$SRC" "$SRC/js_backend/js_backend.h.ct" -o "$IMPL"
ct build --emit-c --ns-root "$SRC" -I "$SRC" <单元列表> -o "$IMPL"
```

两档随后都 rc=0。这条已写进卡 §9，避免后来人「先单元后头」再撞一次。

## 4. 两处 QuickJS 代码并存的处置

现状：`src/js_backend/js_backend_quickjs.ct`（生效）与 `src/js_host/js_host.ct`（旧实现，源码保留）内容几乎一致。

处置：**旧实现标为 deprecated，不删**，文件头写清「已被取代、不要加逻辑、删除的前置条件」。删除不是「以后再说」，而是可核对的三条（写进卡 §10）：

1. quickjs 档在**至少 2 个独立提交**上各有一次全绿记录（不是同一次重复跑）；
2. 这期间 null 档每次都 rc=0；
3. 期间 ⑧⑨/smoke 无一次失败或回退到 SKIP。

三条同时满足后，删除 `src/js_host/` 作为**单独提交**，并在提交信息里贴出满足条件的提交号。

## 5. 本次改动清单

| 文件 | 改动 |
| --- | --- |
| `src/js_backend/js_backend_quickjs.h.ct`、`.ct`（新） | 第二实现 + 两条 CT 约束注释 |
| `tools/check.sh` | 档位切单元、恢复 ⑧⑨/smoke 真跑、②a 先头后单元 |
| `docs/decisions.md` D-21 | 实施状态改为「quickjs 档已接线」 |
| `docs/reviews/nexcore-p1-closeout.md` | 补「该限制已由 P2 解除」的后续注 |
| `docs/reviews/nexcore-p2-card-draft.md` | 状态改「已施工」，新增 §9 实施记录 |
