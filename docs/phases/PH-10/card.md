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
```c
/* src/js_backend/js_backend.h.ct 追加 */
/*
 * 泵 job 队列。max_jobs > 0 时最多跑 max_jobs 个；max_jobs <= 0 表示泵到空，
 * 但受 NXJB_PUMP_CAP（10000）硬上限约束。返回实际执行数（>=0）；job 抛错返回 -1。
 */
pub int32_t pump(void *h, int32_t max_jobs);
```

- **有界**：`max_jobs <= 0` 不得无界循环（自续链 `var f=function(){Promise.resolve().then(f)}; f();` 必须返回）；达到 CAP 即停。
- **`max_jobs > 0`**：只跑前 `max_jobs` 个，剩余留在队列，由下一次 `eval` 的 pre-pump 或 `shutdown` 收尾泵继续。
- **shutdown 收尾泵**：释放 runtime 前执行一次有界泵，避免最后一条消息排下的 job 被静默丢弃。
- **异常显式处理**：job 抛错 → 清异常并返回 `-1`；`eval` 的 pre-pump / post-pump 收到 `-1` 时把本次消息判为 `NXJB_ERR`，不得写成 `(void)pump(...)`。
- 泵仍在 backend 内，宿主入口仍是 `send`+`drain`（不新增导出符号，I-1）。
- 预算：不做 D-05 的 10k 让出配额与 200 ms 窗口；上面的 CAP 只保证「有界」，不是 runaway 判据。
- `turns()` 无调用者，随本卡删除（红队 A17；避免僵尸接口被误当能力）。

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

1. **逐字段判可用性**：`available(F) := struct_size >= offsetof(F) + sizeof(F)`。只读满足 `available` 的字段；`native_call` 可用而 `native_user` 不可用时 user 置 NULL。只有 `native_call`、`native_names`、`native_names_len` 三者都可用且 `len > 0` 才安装 provider。**不得**用 `NEXCORE_CONFIG_CURRENT_SIZE` 判——它随库漂移，下一次尾追加会让旧宿主静默失去 Provider（红队 A3）。
2. **生命周期与所有权**：`native_names` 只在 `nexcore_create` 期间被读，库**必须拷贝**（宿主可在 create 返回后释放）；传给 `native_call` 的 `name` / `args_json` 只在回调期间有效（库临时缓冲），宿主须自行拷贝；`out_json` 由宿主 `malloc`、库 `free`。
3. **名字语义**：挂载路径与回调收到的 `name` 都是**全名**（`Shell.print`），不是末段。
4. **白名单是真校验**：`js_native_call` 派发前线性比对全名；不在清单内 → 抛 `TypeError`，不调用宿主回调。清单在 create 时拷贝成 C 数组；挂载完成后 `__nx_native_names` 从全局删除（脚本读不到清单），`__nx_native` 保留但入口有校验。
5. 调用约定：实参序列化为 JSON 数组；`NX_OK` 时把 `*out_json` 解析为 JS 值并 `free`；非 OK / 解析失败 → 抛错。
6. null 档不创建引擎，native 字段全部忽略，`create` 仍返回 `NX_OK`。

### 4.3 D3：`.a`

```bash
ar rcs "$BIN/libnexcore.a" "$IMPL"/obj/*.o
```

判据见 E5。三条定死：

- 对象集合随档变化：`BACKEND_UNIT` 二选一，null 档**没有** `js_backend_quickjs.o`，quickjs 档**没有** `js_backend.o`（红队 A15：卡面旧文写「同时有」是错的）。
- 消费方必须写归档的**显式路径**，不得 `-lnexcore`（macOS 下 `-l` 优先选 `.dylib`，会让「只链 .a」的判据空转，红队 A12）。
- 归档的 `T` 符号必然多于 7（`-exported_symbols_list` 只对 dylib 生效；实测 19 个）。「7 个导出」只对 dylib 成立；归档判据见 E5②，并在 PH-11 卡写死「宿主只许用 7 个」的纪律。

### 4.4 D4：边界扫描

- `tools/check-boundaries.sh:16-18` 默认目标改为 `("$ROOT/src" "$ROOT/experiments/src" "$ROOT/hosts")`。
- `tools/check.sh` 的 ⑤c 实参改为 `bash tools/check-boundaries.sh "$SRC" experiments/src hosts`。
- **C 宿主同样受检**：`hosts/` 下的 `.c` / `.h` 只允许 `#include <...>` 系统头与 `#include "NexCore.h"`；任何 `#include "..."` 指向 `src/`、`../src` 或工程内其他头一律违规。理由：选 C 宿主的核心理由是「只走公开 ABI」，只扫 `.ct` 覆盖不到真实交付物（红队 A16）。
- 目录不存在时脚本保持 `[ -d "$dir" ] || continue` 跳过；E7 负责保证目录存在。

---

## 5. 任务清单（四要素 + effort；每任务一对提交）

| task-id | deliverable（一条） | verify（该任务自己的命令） | commit（提交内容） | rollback | effort | 归属 |
| --- | --- | --- | --- | --- | --- | --- |
| PH10-T1 | `js_backend` 增 `pump` 且 `eval` 后泵空 | `cc -std=c99 -Wall -Wextra -Werror -I build/abi tests/pump_microtask.c build/bin/libnexcore.dylib -o /tmp/pm && /tmp/pm` 输出 `payload=1`（实现前为 `payload=0`，即红证） | `test(pump): PH10-T1 红` + `feat(pump): PH10-T1 …` | 成对 `git revert`（先实现后夹具），不触及 `NexCore.ct` | M | 库层 |
| PH10-T2 | 门禁 ⑮：微任务可见性 | `bash tools/check.sh` 输出含 `⑮ microtask pump: PASS`；注入空 `pump` 后该行变 FAIL | 同上 | 成对 revert；⑤ 与 ⑩ 不受影响 | S | 门禁层 |
| PH10-T3 | config 尾部 4 字段 + 库内绑定 | `python3 tools/abi_layout.py --header build/abi/NexCore.h --write` 重生成 golden 后，`NEXCORE_JS=quickjs bash tools/check.sh` 全绿（⑪⑫ 均 PASS） | 同上，另含 `abi/abi-layout.txt` 重生成 | 成对 revert；回滚后 ⑪ 需同步回退 golden（同一 revert 内） | M | 库层 |
| PH10-T4 | 门禁 ⑯：Provider 三态 | `bash tools/check.sh` 输出含 `⑯ native provider: PASS` | 同上 | 成对 revert | S | 门禁层 |
| PH10-T5 | `.a` 产出 + 归档面判据 | `ls -l build/bin/libnexcore.a` 非空；`bash tools/check.sh` 输出含 `⑰ archive: PASS`（合格线见 E5，分档判定） | 同上 | 成对 revert；dylib 目标不受影响 | S | 构建层 |
| PH10-T6 | 边界扫描范围加 `hosts`（`.ct` + C 头规则） | `bash tests/hosts_boundary_scan.sh`（内部注入 `.ct` 与 `.c` 两种探针）输出 `HOSTS SCAN OK` | 同上 | 成对 revert；`src`/`experiments` 覆盖不变 | S | 门禁层 |
| PH10-T7 | `hosts/` 目录落位（仅文档） | `ls hosts/README.md` 存在；`bash tools/check-boundaries.sh` PASS 且扫描数 +0（无 `.ct`） | 单提交 `docs(hosts): PH10-T7 …`，`no-test: 纯文档目录占位` | 单提交 revert | S | 文档层 |

**T-5 自检（修订后口径）**：E1/E1b/E1c/E1d 是**同一交付物（泵）**的四条断言，不是四个交付物；其余 E2–E7 与 T2–T7 一一对应。仍不出现「一个任务顶多个交付物」。
**T-2 自检**：无 L/XL；仅 T1/T3 为 M。
**T-7 例外**：仅 PH10-T7 为纯文档，其余 6 项都要求「红证提交 → 实现提交」相邻。

---

## 6. 退出判据（E1–E7，逐条可核对）

| # | 判据 | 判定命令与阈值 | 对应任务 |
| --- | --- | --- | --- |
| E1 | `Promise` 回调在 `send`+`drain` 后**必然**执行过 | `tests/pump_microtask.c`：bootstrap 挂 `.then`，`send "hit"` 后 sink 收到 `1`；两级链 `n == 2`。**quickjs 档 PASS；null 档 N/A（无引擎，不计通过）** | T1 |
| E1b | 泵有界：自续链必须返回 | 同一夹具追加用例：`var f=function(){Promise.resolve().then(f)}; f();` 后 `send`+`drain` 在 CAP（10000）内返回，不挂死；`hits` 计数 == CAP | T1 |
| E1c | `max_jobs > 0` 只跑 N 个，且 `shutdown` 收尾泵不丢 job | 夹具用 provider 计数器：排 3 个 job → `pump(h,1)` 后计数 +1；再 `shutdown` 后计数达到 3（收尾泵补齐） | T1 |
| E1d | job 抛错不静默 | `Promise.resolve().then(function(){throw new Error("boom")})` 之后的下一条消息 `send` 后 sink 无投递，且 `stat(LAST_ERROR)` ≠ 0 | T1 |
| E2 | ⑮ 门禁真跑且能红 | 正例 PASS；把 `js_backend_quickjs.ct` 的 `pump` 换成 `return 0;` 后 ⑮ FAIL（红证贴原始输出与退出码） | T2 |
| E3 | Provider 注册可用且向后兼容 | `tests/native_provider.c`：注册名 JSON 往返正确；未注册名 → JS 抛错；**清单外全名直接调 `__nx_native` 也必须抛错**；老调用方按 **88 / 96 / 104 / 112 / 120** 五档各测一次，每档 `malloc(N)` 精确分配 + 尾页 `PROT_NONE` 保护（越界必崩），期望：N<120 → `create` rc=0 且无 `Shell`；N=120 → 有 `Shell` | T3 |
| E4 | ⑯ 门禁真跑且能红 | 正例 PASS；删掉 `native_call` 赋值后 ⑯ FAIL | T4 |
| E5 | `.a` 存在且内容可核对 | ①`ar t` 只取 `*.o` 成员（排除 `__.SYMDEF SORTED`），集合 == `basename(build/impl/*.c)`；②含全部 7 个 `nexcore_*`，且**其余 `T` 必须落在 `js_backend_` / `queue_` / `protocol_` 三个内部前缀内**（不得出现别的前缀）；③链接用**归档显式路径**，`otool -L` 不得出现 `libnexcore.dylib`；null 档只判「链接成功 + 依赖干净」，**不判** `SMOKE OK`；quickjs 档显式追加 `libquickjs.a`，运行得 `SMOKE OK`；④负例：`ar d NexCore.o` 后链接必须失败，且 stderr 含 `_nexcore_create`，编译命令不得带 `-undefined dynamic_lookup` | T5 |
| E6 | 边界扫描覆盖 `hosts/`（`.ct` 与 C 双向） | ①注入 `hosts/shell/__boundary_probe.ct`（`#if 1`）：默认扫描 rc=1 且点名该文件，删除后 rc=0；②注入 `hosts/shell/__probe.c`（`#include "../../src/NexCore.h"`）：rc=1 且点名，删除后 rc=0 | T6 |
| E7 | `hosts/` 目录存在且不引入 `.ct` | `find hosts -name '*.ct'` 为空（目录本身必须存在）；`bash tools/check-boundaries.sh` PASS | T7 |

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
| PH10-T3 | `aed0cda` | `cc -std=c99 -Wall -Wextra -Werror -I build/abi tests/native_provider.c build/bin/libnexcore.dylib -o /tmp/np_base` | `error: no member named 'native_call' in 'struct NexCoreConfig'`（native_user/native_names 同） | 1 | 2026-11-15 |
| PH10-T4 | `9059e33` | 副本里把 `native_call = cfg->native_call;` 改成 `native_call = 0;` 后跑 `NEXCORE_JS=quickjs bash tools/check.sh` | `A1 FAIL: Shell.echo(7) -> <none>` / `⑯ FAIL: 原生 Provider 未生效或越界读了尾字段` | 1 | 2026-11-15 |
| PH10-T5 | `799a4e3` | 只加 ⑰（不加 `ar` 目标）后跑 `NEXCORE_JS=quickjs bash tools/check.sh` | `⑰ FAIL: build/bin/libnexcore.a 不存在（构建未产出静态归档）` | 1 | 2026-11-15 |
| PH10-T6 | `19b7934` | `bash tests/hosts_boundary_scan.sh` | `FAIL: check-boundaries.sh 默认范围没扫到 hosts/（rc=0）` / `boundaries: PASS（19 个文件…）` | 1 | 2026-11-15 |
| PH10-T7 | `6881cf0` | `find hosts -name '*.ct'` 与 `bash tools/check-boundaries.sh` | 纯文档目录：无 `.ct`，扫描仍 PASS（no-test，理由见任务行） | 0 | 2026-11-15 |

---

## 12. 改卡记录（2026-11-15；T-6 冻结的授权例外）

依据：`docs/reviews/nexcore-shell-host-stage0-redteam.md` §2–§7 + 用户五条裁定。逐条记「改了什么 / 为什么必须 / 原判据为什么不行」，不作静默替换。

| # | 改了什么 | 为什么必须 | 原判据为什么不行 |
| --- | --- | --- | --- |
| C1 | E3 老调用方判据：`struct_size` **逐字段** `>= offsetof(F)+sizeof(F)`；补 88/96/104/112/120 五档 | 88 是唯一真实存在的旧尺寸；按字段判不随库漂移 | 卡面「< `offsetof(native_call)`」＝88 档被判为「可读」，读 88..95 越界（ASan 实测）；实现用 `>= CURRENT_SIZE` 则下次尾追加后 120 字节宿主静默失去 Provider |
| C2 | E3 夹具改成「精确 `malloc(N)` + 尾页 `PROT_NONE`」 | 越界必须真越出分配边界才叫证明 | 原夹具分配 120 却声明 72，越界落在自己块内，ASan 抓不到；断言 `typeof Shell` 也区分不了，是假绿 |
| C3 | D2 白名单改为**派发点真校验**，并写清三种字符串生命周期与「name 是全名」 | 原实现 `__nx_native` 常驻且不校验，脚本可调清单外名字，「白名单」名不副实 | 只有挂载清单，没有权限边界；且 `native_names` 指针被存进 state、宿主释放即悬垂 |
| C4 | D1 泵：`max_jobs<=0` 以 `NXJB_PUMP_CAP=10000` 封顶；`shutdown` 补收尾泵；job 异常映射为 `NXJB_ERR`；新增 E1b/E1c/E1d | 无界循环会让自续链永久挂死；`(void)pump` 把错误吞成假绿；shutdown 不泵会丢尾 job | 原卡只写「泵到空」+ 注释，三处边界都无夹具；`eval` 里的 `(void)` 是明示的忽略 |
| C5 | E5 重写：排除 `__.SYMDEF`；`T` 符号按内部前缀约束；归档显式路径 + 禁 `-lnexcore`；null/quickjs 分档；负例断言 stderr 含 `_nexcore_create` | 原判据两档都不可能、且可被链接选项绕过 | null 档跑不到 `SMOKE OK`（run_rc=6）；quickjs 档缺引擎必链接失败；`-lnexcore` 选 dylib；`-undefined dynamic_lookup` 能让缺成员的归档照样链接；`ar t` 含 `__.SYMDEF` 导致恒红 |
| C6 | D4 增加 C 宿主规则（`hosts/**/*.c|*.h` 只许系统头与 `"NexCore.h"`），E6 改为注入 `.ct` 与 `.c` 两种探针 | 真实交付物是 C，只扫 `.ct` 等于不覆盖 | 原 E6 用永远不会出现的 `.ct` 自证；T6 的「scanned 增长」与 E7「无 `.ct`」互斥 |
| C7 | 删除 `turns()`（接口、两档实现、`units.json`、map） | 无调用者的僵尸方法会被下游误当能力 | 原卡保留它且无判据 |
| C8 | ⑯ 失败文案改为「Provider 未生效 / 越界读尾字段（五档夹具覆盖）」 | 文案必须与实际覆盖面对齐 | 原文案声称覆盖越界，但夹具抓不到 |
| C9 | T-5 自检口径澄清：E1/E1b/E1c/E1d 属同一交付物 | 避免「一任务多交付物」的误读 | 原文「7 任务 ↔ E1–E7 一一对应」在拆出子断言后不成立 |

**返工范围**：T1–T6 已落地的实现需按 C1–C8 返工（T7 不受影响）；返工提交紧随本卡修订之后，仍按「红证 → 实现」成对提交。
| PH10-T3r | `6094568` | 把 `NexCore.ct` 的阈值临时改成字面的 `struct_size >= 88` 后跑 `tests/native_provider.c`（五档保护页夹具） | 无输出即被信号终止：`rc=138`（SIGBUS，88 档踩 PROT_NONE 页） | 138 | 2026-11-15 |

| PH10-T1r | `4dc4c84` | 把泵改回无界（`limit = 0x7fffffff`）、去掉 shutdown 收尾泵后跑 `tests/pump_microtask.c` | `C: bounded...` 不出现即挂死；`rc=142`（SIGALRM，alarm(5) 兜底） | 142 | 2026-11-15 |
| PH10-T1s | `4dc4c84` | 同上环境跑 `tests/pump_bounds.c`（白盒） | `B: after shutdown bumps=1` / `FAIL: shutdown 未补齐剩余 job（期望 bumps=3）` | 4 | 2026-11-15 |
| PH10-T6r | `3084fc3` | `bash tests/hosts_boundary_scan.sh`（新增 `.c` 越界 include 探针） | `FAIL: C 宿主越界 include 未被拦下（rc=0）` / `boundaries: PASS（19 个文件…）` | 1 | 2026-11-15 |
