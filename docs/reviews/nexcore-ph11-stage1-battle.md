# PH-11（shell 宿主阶段 1）两席对打与裁定

> 状态：**已裁定（2026-11-15）**，作为 `docs/phases/PH-11/card.md` 的设计依据。
> 上游：[nexcore-shell-host-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md) §6.2（阶段 1 与两项库层任务）、[nexcore-shell-host-proposal-revisions.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-proposal-revisions.md) 第 8/10/12 条。
> 纪律：两席只依据可复跑的事实；结论落卡时逐条转化，不在此处写实现。

---

## 0. 争点

阶段 1 要跑「脚本文件」，而库今天只会求值**单个表达式**。两处必须定死：

| # | 争点 | 选项 |
| --- | --- | --- |
| Q1 | 程序模式（多语句 / `#!`）怎么进库 | A 表达式优先、失败回退程序；B `NexCoreConfig` 尾字段显式声明模式；C 默认改程序模式 |
| Q2 | 错误文案怎么出库 | A 经 sink 投 `$error` 通知；B 新增 ABI 函数取文案；C 只给 `stat(LAST_ERROR)` 整数 |

---

## 1. 实测（`/tmp/nx11probe/probe.c`，QuickJS 2025-09-13，`ct v0.44.2` 基线同款库）

命令：`cc -std=c99 -I experiments/third_party/quickjs probe.c libquickjs.a -lm -lpthread -o probe && ./probe`

| 输入 | 求值方式 | 结果 |
| --- | --- | --- |
| `(var a=1; a+1)` | 现行表达式包裹 | `SyntaxError: unexpected token in expression: 'var'` |
| `var a=1; a+1` | 程序模式 | `2` |
| `#!/usr/bin/env nex\nvar b=2; b+1` | 程序模式 | `3`（引擎自己跳过 `#!` 首行） |
| `(#!...\n2+3)` | 表达式包裹 | `SyntaxError: invalid first character of private name` |
| `{id:1}` | 程序模式 | `1`（当作块语句，完成值 1） |
| `({id:1})` | 表达式包裹 | `[object Object]`（现行行为） |
| `throw new Error('boom')` | 程序模式 | `Error: boom`（`JS_ToCString` 可取到文案） |

三条硬事实：

1. `#!` **不需要库自己剥**：程序模式下引擎直接接受；表达式模式下无论怎么包都不行。
2. 程序模式会改变 `{...}` 的语义（块 vs 对象字面量）；今天 `send "{\"id\":1}"` 依赖表达式模式。
3. 异常文案可由 `JS_ToCString(JS_GetException(ctx))` 取出——现在被取到即 free，所以宿主看不到。

---

## 2. 两席

### 2.1 主张席（零 ABI 变更）

**Q1 选 A（表达式优先、SyntaxError 回退程序）**：
- 不新增 `NexCoreConfig` 字段，不重生成 golden，不碰五档 `struct_size` 协商；PH-10 刚收口的面不动。
- 向后兼容是**构造性**的：表达式能解析就一定走原路径，移动端宿主的现有消息语义不变。
- 回退只在解析失败时发生，语法错误阶段没有副作用，重试安全。
- `#!` 自动被程序模式接受，宿主不必自己剥首行。

**Q2 选 A（`$error` 通知经 sink）**：
- sink 是唯一的出站通道，协议里已有 `$error` 通知形状（[protocol.md:137](file:///Users/samxx/trae-ws/nexcore/docs/protocol.md#L137)）与 `E_JS_EXCEPTION` 码（[protocol.md:109](file:///Users/samxx/trae-ws/nexcore/docs/protocol.md#L109)），不新增协议码。
- 无新导出符号，I-1 不动；宿主的 exit code 用 `stat(LAST_ERROR)` 判，文案用 sink 打。

### 2.2 反证席（可判定性优先）

**Q1 反对 A，主张 B（显式模式）**：
- 自动回退让「一条消息到底按表达式还是按程序求值」取决于解析结果，宿主无法预先知道，也无法测「我要求的是程序模式」。判据只能写「结果对」，写不出「模式对」。
- 双重解析会让**真语法错**报第二遍的错误（程序模式），行号与提示可能指向与作者意图不同的位置。
- 回退依赖「表达式模式一定先失败」这一引擎细节；换引擎或换 `JS_EVAL_TYPE_*` 组合就可能漂移，属隐式耦合。
- `{id:1}` 这类输入在两种模式下结果不同——虽然今天偏表达式，但这是**歧义**，不是兼容。

**Q2 反对 A，主张 B（新增 ABI 错误面）**：
- 把错误塞进 sink，会让「响应流」里混入非响应通知；移动端宿主的 sink 解析要先判是不是 `$error`，等于给所有宿主加一个分支。
- `LAST_ERROR` 只有整数，宿主分不清「语法错」与「运行时错」，也拿不到行号。

### 2.3 对打焦点

| 焦点 | 主张席回应 | 反证席回应 | 裁定依据 |
| --- | --- | --- | --- |
| 模式可测性 | 判据测行为即可：`var a=1; a+1 → 2`、`#! → 3`；「模式正确」不是用户可见语义 | 行为相同不代表语义相同：`{id:1}` 在两种模式下都「成功」，值不同 | **反证席胜**：`{id:1}` 一例给出两种不同结果，说明「自动」制造了不可判定的二义 |
| 兼容成本 | B 要动 config 尾字段 + golden + 五档协商 | PH-10 已把机制建好（`NEXCORE_CONFIG_NATIVE_SIZE`、逐字段判可用），增量是常数级 | **主张席部分胜**：成本真实但已被制度化，不构成否决理由 |
| 引擎耦合 | `#!` 由引擎接受是实测事实 | 实测只覆盖当前版本；表达式包裹失败也是实测 | 平手：两条都依赖引擎行为，但 B 不依赖「失败」这一负向行为 |
| 错误载体 | `$error` 是协议既有形状，且 sink 本就是唯一出站口 | 混流让所有宿主加分支；ABI 加函数则破坏 7 符号 | **主张席胜**：`$error` 零新增符号、零新增协议码；反证席的 B 直接撞 I-1 |

---

## 3. 裁定

### 3.1 Q1：选 **B（显式模式）**，但按 PH-10 的尾字段纪律追加

- `NexCoreConfig` 尾部追加 `int32_t eval_mode`（`0` = 表达式，`1` = 程序，缺省 `0`）。
- 可用性判据沿用 PH-10 C1：`struct_size >= offsetof(eval_mode) + sizeof(eval_mode)`；不新增 `NX_E_*`。
- 程序模式下落 `#!` 首行（引擎已接受，库不额外剥；只在文档写清）。
- 表达式模式行为**逐字节不变**（仍包 `(` `)`），现有 ⑧⑨/smoke 与移动端语义不受影响。
- `{id:1}` 的歧义由**调用方**决定：不设该字段（或置 0）就是对象字面量，置 1 就是块语句。宿主 `nex` 置 1。

**为什么不选 A**：`{id:1}` 的两种结果足以证伪「自动探测对用户透明」；判据要能二值判定，就必须让模式由输入显式决定。
**A 的复活条件**：若将来出现「宿主无法在 create 期知道消息性质」的场景（例如同一 handle 混跑表达式与脚本），再评估按消息带模式；届时属新的 ABI 契约，须重走 D-12/D-18。

### 3.2 Q2：选 **A（`$error` 通知经 sink）+ `LAST_ERROR` 置码**

- eval 失败时，`NexCore.ct` 把 `{"code":"E_JS_EXCEPTION","message":"<引擎文案>"}` 推进 outbox，经 sink 投递；`stat(LAST_ERROR)` 同时置 `NX_E_INTERNAL`。
- 不新增导出函数（I-1 不破），不新增协议错误码（`E_JS_EXCEPTION` 已存在）。
- **不新增 ABI 错误码 `NX_E_JS_EXCEPTION`**：ABI 码是跨端契约，加码要让 Swift/Kotlin 侧同步；列 PH-12 候选，理由写进卡面。
- 抑制无限循环：同一 `send` 只产出一条 `$error`；`$error` 本身不再触发错误投递。

**为什么不选 B**：新增 `nexcore_last_error_message()` 直接撞 I-1（7 符号冻结）；用 `stat` 扩展键只能带整数，无法满足「文案/堆栈」。

### 3.3 对被否方案的处理（不静默丢）

| 方案 | 处置 |
| --- | --- |
| Q1-A 自动回退 | 否决，复活条件已写；卡面 §非目标 记一条 |
| Q2-B 新 ABI 函数 | 否决（I-1）；若移动端确需结构化错误，走 PH-12 的 ABI 错误码评审 |
| Q2-C 只有整数码 | 否决（不满足阶段 1 的「打印异常」出口） |

---

## 4. 对卡的三条硬约束

1. **归属**：程序模式与错误面都改 `src/js_backend/` 与 `src/NexCore.ct`，属**库层任务**，移动端同样受益；不得记在宿主名下。
2. **兼容**：`eval_mode` 缺省 0 时，`{id:1}` 必须仍是对象字面量；⑧⑨/smoke 与 PH-10 的 ⑮⑯⑰ 不得回归。
3. **宿主只消费**：`hosts/shell/main.c` 只 `#include "NexCore.h"`，不 import 库内单元；不新增门禁编号，宿主用例走独立 `tools/check-host.sh`。
