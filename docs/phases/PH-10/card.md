# PH-10 卡：Shell 宿主前置——库层契约与构建面（裁定后的「阶段 0」）

> 状态：**已冻结（2026-11-15）**，待 REQ 签批后开工。
> 上游：[nexcore-shell-host-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md)（A/B 裁定、阶段 0 插入、四项开放问题）
> 关联：[decisions.md D-21](file:///Users/samxx/trae-ws/nexcore/docs/decisions.md#L402-L412)、D-12（唯一跨 ABI 结构体）、D-18（`struct_size` 尾部追加）
> 冻结面：本卡的**任务清单**与**退出判据**自签批起不可改（6.6 T-6）；`version-minor` 同受冻结（6.7）。
> 禁止改动：`docs/roadmap.md`、`docs/acceptance.md`、`abi/expected-forward-decls.txt` 的条目内容、`.ai-engineering`。

---

## 1. 编号与归属

**编号取 `PH-10`**，理由是可机械核对的：`g1-version` 要求 `version-minor == n`，产品版本现为 `v0.9`（`docs/README.md:17`，MINOR=9），V-1 要求 MINOR 相对上一 phase 恰好 +1，因此首个 phase 只能取 `n=10`。**不登记版本偏离**（`d=0`）。

**本卡对应的业务阶段**是裁定文档里的「阶段 0」。为避免与 `roadmap.md` 的 P1–P4（框架阶段，已冻结）混称，本文件一律用 `PH-10`，只在 §10 说明它与后续「阶段 1（=PH-11）」的关系。

### 1.1 归属声明（本卡第 4 条要求）

本卡四项交付物**没有一件属于 shell 宿主**。清单如下，施工时不得把它们写进宿主的名义：

| 交付物 | 归属 | 依据 | 谁受益 |
| --- | --- | --- | --- |
| 微任务泵 | **库层**（`src/js_backend/`） | 泵的位置在 `eval` 之后，属引擎语义 | 移动端同样受益：今天 `Promise` 在 Swift/Kotlin 宿主里也是死的（实测 `payload=0`），不是 CLI 独有 |
| Provider 注册 | **库层**（ABI 头 + `src/`） | 注册是跨宿主能力，CLI 只是第一个使用者 | 移动端同样受益：`Shell.*` 之外，任何宿主原生能力注入都要走它 |
| `libnexcore.a` | **构建层**（`tools/check.sh`） | 产物形态，与宿主语言无关 | 分发形态统一；宿主只是消费者 |
| `hosts/` 纳入边界扫描 | **门禁层**（`tools/check-boundaries.sh` + `tools/check.sh`） | 扫描范围是仓库纪律 | 与宿主无关；防的是「新目录即盲区」 |

**结论**：PH-10 是**库与工具链的 phase**。它的产出即使没有 shell 宿主也要做；shell 宿主只是把「必须做」的时间点提前了。

---

## 2. 不变量（施工时必须逐条守住）

| # | 不变量 | 核对方式 |
| --- | --- | --- |
| I-1 | 导出 ABI 函数仍是 **7 个**，不增不减 | `nm -gU build/bin/libnexcore.dylib` 的 `T` 行仍为 7；`check.sh` ⑩ 不改阈值 |
| I-2 | 跨 ABI 结构体仍**只有** `NexCoreConfig` | `grep -nE '^struct [A-Za-z0-9_]+;' build/abi/NexCore.h` 只出 `NexCoreConfig`；`abi/expected-forward-decls.txt` 不改 |
| I-3 | 新字段一律**尾部追加**，`NEXCORE_CONFIG_MIN_SIZE` 不变 | `NEXCORE_CONFIG_MIN_SIZE` 仍等于 `offsetof(bootstrap_len)+sizeof(size_t)`；老 `struct_size` 调用方行为不变（⑫ 继续 PASS） |
| I-4 | `NEXCORE_ABI_VERSION` **不升**（D-18：可选尾字段只更新 CURRENT_SIZE） | `src/NexCore.h.ct` 里 `NEXCORE_ABI_VERSION = 1` 不变 |
| I-5 | 两档（null / quickjs）都 rc=0 | `bash tools/check.sh` 与 `NEXCORE_JS=quickjs bash tools/check.sh` 末行都 `ALL CHECKS PASSED (…)` |
| I-6 | 不修改 `docs/acceptance.md`、`docs/roadmap.md` | `git diff --name-only` 不含这两个路径 |

**冲突预案**：若施工中发现某条判据必须改 `acceptance.md` 才能成立，**停手上报**，不得自行改条目。本卡已按 I-1/I-2 选择「不新增 ABI 函数」的实现路径来规避该冲突。

---

## 3. 交付物

| # | 交付物 | 形态 |
| --- | --- | --- |
| D1 | 微任务泵的宿主入口 | `js_backend` 新增 `pump` 方法 + `eval` 之后泵空；**宿主可见入口复用 `nexcore_drain`**，不新增导出符号 |
| D2 | 原生 Provider 注册进 ABI | `NexCoreConfig` 尾部追加 4 个标量字段（不新增 struct、不新增函数），单回调 + 名字白名单 |
| D3 | `.a` 产物 | `tools/check.sh` 新增 `ar` 目标 + 归档面判据 + 负例 |
| D4 | `hosts/` 纳入边界扫描 | `check-boundaries.sh` 默认目标与 `check.sh` ⑤c 实参同时加 `hosts`；`hosts/` 目录本身落位 |

---

## 4. 设计定死项（施工方无须再设计，照此实现）

### 4.1 D1：微任务泵

```c
/* src/js_backend/js_backend.h.ct 追加 */
/*
 * 把 runtime 的 job 队列泵到空或泵满 max_jobs 个（max_jobs <= 0 视为「泵到空」）。
 * 返回实际执行 job 数（>=0）；出错返回 -1。job 内的异常不得穿透到调用方。
 */
pub int32_t pump(void *h, int32_t max_jobs);
```

- quickjs 档实现：`while (ran < max_jobs && JS_ExecutePendingJob(rt, &ctx) > 0) ran++;`，`JSValue` 返回值必须 `JS_FreeValue`；`JS_ExecutePendingJob` 返回负数时 `ran` 不变并返回 `-1`。
- null 档实现：恒返回 `0`。
- `eval` 成功后**必须**调用 `pump(h, 0)`（泵到空），再返回结果；`eval` 失败不泵。
- `NexCore.ct` 的 `pump_inbox` 在 `eval` 之后无需额外改动（泵在 backend 内完成）；`nexcore_drain` 的语义因此变成「投递 outbox 前，微任务已清空」，这就是**宿主入口**：宿主只需 `send` + `drain`。
- 预算：PH-10 只做「泵到空」，**不做** D-05 的 10k 让出配额与 200 ms 窗口（那是 P2 的 runaway 预算）。本卡不声称任何 runaway 判据成立。

### 4.2 D2：Provider 注册

`src/NexCore.h.ct` 追加（**只在 `log_user` 之后**，保持 I-3）：

```c
pub typedef int32_t (*NexCoreNativeCall)(void *user,
                                         const char *name, size_t name_len,
                                         const char *args_json, size_t args_len,
                                         char **out_json, size_t *out_len);

pub struct NexCoreConfig {
    /* …既有字段不动… */
    NexCoreNativeCall native_call;   /* 新增：NULL = 不注册任何原生能力 */
    void *native_user;               /* 新增 */
    const char *native_names;        /* 新增：JSON 字符串数组，如 ["Shell.print","Shell.execSync"] */
    size_t native_names_len;         /* 新增；为 0 视为未提供 */
}
```

语义（实现按此，不另行解释）：

1. `struct_size` 小于 `offsetof(native_call)` 的调用方**视为未提供**，库不得读越界（⑫ 的「> CURRENT 尾垃圾」用例继续覆盖相反方向）。
2. `native_call == NULL` 或 `native_names_len == 0` → 不注册，JS 侧不出现任何新全局。
3. 名字按 `.` 分段挂到全局（`Shell.print` → `globalThis.Shell.print`）；父对象不存在则创建普通对象。
4. 调用约定：C 侧 trampoline 把实参序列化为 JSON 数组（`[a,b,c]`）传给 `native_call`；返回 `NX_OK` 时把 `*out_json` 当 JS 值解析后作为调用结果，并 `free(*out_json)`；非 `NX_OK` 或解析失败 → 抛 `Error("native " + name + " failed")`。
5. null 档不创建引擎，`native_call` 被忽略，`create` 仍返回 `NX_OK`。

### 4.3 D3：`.a`

```bash
ar rcs "$BIN/libnexcore.a" "$IMPL"/obj/*.o
```

判据见 E5；注意 quickjs 档会同时有 `js_backend.o`（空壳，0 个定义符号）与 `js_backend_quickjs.o`，**不要求固定 4 个对象**，只要求对象集合等于该档 `build/impl/*.c` 的派生集合。

### 4.4 D4：边界扫描

- `tools/check-boundaries.sh:16-18` 默认目标改为 `("$ROOT/src" "$ROOT/experiments/src" "$ROOT/hosts")`。
- `tools/check.sh` 的 ⑤c 实参改为 `bash tools/check-boundaries.sh "$SRC" experiments/src hosts`。
- 目录不存在时脚本已有 `[ -d "$dir" ] || continue`，保持该行为（不得为了「扫得到」而强制目录存在）。

---

## 5. 任务清单（四要素 + effort；每任务一对提交）

| task-id | deliverable（一条） | verify（该任务自己的命令） | commit（提交内容） | rollback | effort | 归属 |
| --- | --- | --- | --- | --- | --- | --- |
| PH10-T1 | `js_backend` 增 `pump` 且 `eval` 后泵空 | `cc -std=c99 -Wall -Wextra -Werror -I build/abi tests/pump_microtask.c build/bin/libnexcore.dylib -o /tmp/pm && /tmp/pm` 输出 `payload=1`（实现前为 `payload=0`，即红证） | `test(pump): PH10-T1 红` + `feat(pump): PH10-T1 …` | 成对 `git revert`（先实现后夹具），不触及 `NexCore.ct` | M | 库层 |
| PH10-T2 | 门禁 ⑮：微任务可见性 | `bash tools/check.sh` 输出含 `⑮ microtask pump: PASS`；注入空 `pump` 后该行变 FAIL | 同上 | 成对 revert；⑤ 与 ⑩ 不受影响 | S | 门禁层 |
| PH10-T3 | config 尾部 4 字段 + 库内绑定 | `python3 tools/abi_layout.py --header build/abi/NexCore.h --write` 重生成 golden 后，`NEXCORE_JS=quickjs bash tools/check.sh` 全绿（⑪⑫ 均 PASS） | 同上，另含 `abi/abi-layout.txt` 重生成 | 成对 revert；回滚后 ⑪ 需同步回退 golden（同一 revert 内） | M | 库层 |
| PH10-T4 | 门禁 ⑯：Provider 三态 | `bash tools/check.sh` 输出含 `⑯ native provider: PASS` | 同上 | 成对 revert | S | 门禁层 |
| PH10-T5 | `.a` 产出 + 归档面判据 | `ls -l build/bin/libnexcore.a` 非空；`bash tools/check.sh` 输出含 `⑰ archive: PASS` | 同上 | 成对 revert；dylib 目标不受影响 | S | 构建层 |
| PH10-T6 | 边界扫描范围加 `hosts` | `bash tools/check-boundaries.sh` 末行含 `3` 个目标目录语义（以 `scanned` 计数增长证明）；注入违规后 ⑤c rc=1 | 同上 | 成对 revert；`src`/`experiments` 覆盖不变 | S | 门禁层 |
| PH10-T7 | `hosts/` 目录落位（仅文档） | `ls hosts/README.md` 存在；`bash tools/check-boundaries.sh` PASS 且扫描数 +0（无 `.ct`） | 单提交 `docs(hosts): PH10-T7 …`，`no-test: 纯文档目录占位` | 单提交 revert | S | 文档层 |

**T-5 自检**：7 项任务与 E1–E7 一一对应，无一项同时覆盖 ≥2 条退出判据。
**T-2 自检**：无 L/XL；仅 T1/T3 为 M。
**T-7 例外**：仅 PH10-T7 为纯文档，其余 6 项都要求「红证提交 → 实现提交」相邻。

---

## 6. 退出判据（E1–E7，逐条可核对）

| # | 判据 | 判定命令与阈值 | 对应任务 |
| --- | --- | --- | --- |
| E1 | `Promise` 回调在 `send`+`drain` 后**必然**执行过 | 用 `tests/pump_microtask.c`（本卡新增）：bootstrap 挂 `.then`，`send "hit"` 后 sink 收到 `1`；两档都要 PASS | T1 |
| E2 | ⑮ 门禁真跑且能红 | 正例 PASS；把 `js_backend_quickjs.ct` 的 `pump` 换成 `return 0;` 后 ⑮ FAIL（红证贴原始输出与退出码） | T2 |
| E3 | Provider 注册可用且向后兼容 | `tests/native_provider.c`：注册 `Shell.echo` → JS 返回 JSON 往返正确；未注册名 → JS 抛错；`struct_size = NEXCORE_CONFIG_MIN_SIZE` 的老调用方 → `create` rc=0 且无 `Shell` 全局 | T3 |
| E4 | ⑯ 门禁真跑且能红 | 正例 PASS；删掉 `native_call` 赋值后 ⑯ FAIL | T4 |
| E5 | `.a` 存在且内容可核对 | ①`ar t build/bin/libnexcore.a` 的对象集合 == `basename(build/impl/*.c)` 集合；②`nm -g --defined-only` 含全部 7 个 `nexcore_*`；③`nm -u` 的未定义符号集合 ⊆ (libc/pthread ∪ 档内 `_JS_*` ∪ **该归档自身已定义的符号集合**)——第三条是必须的，因为 `NexCore.o` 会引用同归档里的 `queue_*`/`js_backend_*`；④`tests/smoke.c` 只链 `.a` 能跑出 `SMOKE OK`；⑤负例：`ar d` 掉 `NexCore.o` 后链接必须失败（rc≠0） | T5 |
| E6 | 边界扫描覆盖 `hosts/` | 在 `hosts/shell/` 放一个 `#if 1` 的 `.ct`：`bash tools/check-boundaries.sh` rc=1 且报出该文件；删掉后 rc=0 | T6 |
| E7 | `hosts/` 目录存在且不引入 `.ct` | `find hosts -name '*.ct'` 输出为空；`bash tools/check-boundaries.sh` PASS | T7 |

**UNVERIFIED 规则**：任何一条无法在本机执行（缺工具链/缺环境），写 `UNVERIFIED` + 原因 + 需要的环境，**不计通过**；本卡不设「跳过算绿」。已知风险：`.a` 的 `nm -u` 白名单在 macOS clang 下可能带 `___stack_chk_*` 等运行时符号，白名单必须**由实测集合**生成并写进脚本，不得凭猜。

---

## 7. 红证要求（T-7，逐任务）

每个任务两个相邻提交，格式与 `AGENTS.md` 6.6 T-7 一致：

```
<impl-sha>  feat(<scope>): PH10-T<k> <一句话>
<test-sha>  test(<scope>): PH10-T<k> 红
```

红证最小字段写进本卡 §9 的对照表：`baseline: <sha>`（= `test` 提交的父提交）、命令、原始输出、退出码。**用实现后的工作树复跑不算红证。**

---

## 8. 非目标（本卡明确不做）

- 不做程序模式求值（多语句/`#!`）、不做异常文案透出——留给 PH-11，见 §10。
- 不做 D-05 的 10k 让出配额、200 ms 窗口、`E_JS_RUNAWAY`。
- 不做 `Shell.spawn`、不做异步子进程、不做信号处理。
- 不新增任何导出 ABI 函数（I-1）；不新增跨 ABI 结构体（I-2）。
- 不改 `docs/roadmap.md`、`docs/acceptance.md`、`.ai-engineering`。
- 不写 `hosts/shell/` 的任何 C 代码（PH-11 才写）。

---

## 9. 收口证据表（收口时逐行填真实输出；**本表不是待补设计**）

设计与判据已在 §4/§6 定死；本表只是收口动作的证据落点，收口时每一行必须是真实命令输出，不允许留空或写「同左」。

| # | 判据 | 命令 | 原始输出摘要 | rc | 结论 |
| --- | --- | --- | --- | --- | --- |
| E1 | … | `…` | `…` | … | — |
| E2 | … | `…` | `…` | … | — |
| E3 | … | `…` | `…` | … | — |
| E4 | … | `…` | `…` | … | — |
| E5 | … | `…` | `…` | … | — |
| E6 | … | `…` | `…` | … | — |
| E7 | … | `…` | `…` | … | — |

---

## 10. 下一阶段（PH-11 = 裁定里的「阶段 1」）入口条件（冻结，先行公布）

**出卡时机**：PH-10 收口（E1–E7 全部为 PASS，无 UNVERIFIED）之后才出 PH-11 卡。**现在不出卡**。

**入口条件（四条全绿才允许开工，缺一不许）**：

| # | 条件 | 核验命令 |
| --- | --- | --- |
| G-1 | E1 与 E2 成立：`Promise` 在 `send`+`drain` 后可观测执行，且 ⑮ 能红 | `bash tools/check.sh` 含 `⑮ microtask pump: PASS` |
| G-2 | E3 与 E4 成立：Provider 注册可用、向后兼容、⑯ 能红 | `bash tools/check.sh` 含 `⑯ native provider: PASS` |
| G-3 | E5 成立：`libnexcore.a` 可被独立 C 程序链接并跑通 smoke | `ar t build/bin/libnexcore.a` 与 `tests/smoke.c` 链接运行 |
| G-4 | E6 与 E7 成立：`hosts/` 已被边界门禁覆盖且目录存在 | 注入违规 → ⑤c rc=1；`find hosts -name '*.ct'` 为空 |

**必须写进 PH-11 卡的一句事实**：PH-11 **不是**「对 `src/` 零修改」。程序模式求值（去掉 `eval` 的括号包裹）与异常文案透出都必须在 `src/js_backend/` 里做，所以 PH-11 有两项**库层任务**（移动端同样受益），另加宿主任务。裁定文档 §6.2 已按此重排，提案原文的「阶段 1 对 src 零修改」不成立（见修订清单第 12 条）。

---

## 11. 红证记录（逐任务追加；`baseline` 必须是该任务 `test` 提交的父提交）

| task | baseline | 命令 | 原始输出（节选） | rc | 时点 |
| --- | --- | --- | --- | --- | --- |
| PH10-T1 | `ced05d4` | `cc -std=c99 -Wall -Wextra -Werror -I build/abi tests/pump_microtask.c build/bin/libnexcore.dylib -o /tmp/pm_base && /tmp/pm_base` | `A: send+drain rc=0 hits=0` / `FAIL: microtasks not pumped (expect hits=1)` | 3 | 2026-11-15 |
| PH10-T2 | `d1c483b` | 在副本里把 `pump` 改成 `return 0;` 后跑 `NEXCORE_JS=quickjs bash tools/check.sh` | `FAIL: microtasks not pumped (expect hits=1)` / `⑮ FAIL: 微任务未被泵到（Promise 回调不可见）` | 1 | 2026-11-15 |
