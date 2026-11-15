# PH-13 遗留 ABI 面评审：K-6（异步/流式 `Shell.spawn`）与 K-7（中断错误可判定）

> 状态：**评审与定案建议，未实施**。日期 2026-11-16。
> 范围：只定面。不改实现、不改 `tools/check.sh`、不改 `docs/roadmap.md`、不改 `docs/acceptance.md`、不 push、不打 tag。
> 上游：[PH-13 卡](../phases/PH-13/card.md) §3.4/§6/§10、[K-3 评审](nexcore-ph12-k3-abi-error-code-review.md) §4/§5、[decisions.md](../decisions.md) D-18/D-22、[acceptance.md](../acceptance.md) ACC-A-02/ACC-A-10/ACC-A-12/ACC-B-02。

**读法**：标【事实】的句子都有 `路径:行号` 与原文；标【推断】的是本评审的判断；标【未核实】的是本轮无法取证的部分。事实与结论不混排。

---

## 0. 结论先写

| 项 | 推荐定案 | 是否动 ABI |
| --- | --- | --- |
| K-6 异步/流式 `Shell.spawn` | **不新增任何 ABI 符号/结构/回调**；异步 MVP 走「宿主持有子进程 + 现有 `nexcore_send`/`nexcore_drain` 回投 + bootstrap JS Promise 包装」 | 零 |
| K-7 中断错误不可判 | **在协议错误码域分流**：预算超时复用 `E_TIMEOUT`，宿主中断新增 `E_INTERRUPTED`；`LAST_ERROR` 维持 `NX_E_INTERNAL` | 零 |
| 是否开新 phase | K-7 建议开 **PH-14**（要改协议与验收契约）；K-6 建议开 **PH-15**，但它的前置是两项既有契约缺口，不是 ABI 评审 | 无 |
| 需要用户裁决 | 5 条，见 §5.3 | — |

一句话：**PH-13 卡 §3.4 写的「异步/流式子进程需要新 ABI」在本仓的定案下不成立**。它只在「把子进程所有权搬进库」时才成立，而那与 D4 的宿主侧 Provider 结论冲突。K-7 同理：K-3 冻结的是 ABI 整数码，不是协议字符串码。

---

## 1. 事实基线

### 1.1 ABI 面现状

| # | 事实 | 出处 |
| --- | --- | --- |
| F1 | 导出函数固定 7 个：`create/set_sink/send/drain/shutdown/free/stat` | `docs/design.md:508`「### 6.2 函数清单（7 个）」；`tools/check.sh:267-282` 的 `ABI_SYMS` 逐名断言，末行「PASS (7/7, T…)」 |
| F2 | 验收侧把 7 个符号写成白名单判据 | `docs/acceptance.md:64`「只出现文档列出的 7 个 `nexcore_*`」 |
| F3 | `NexCoreConfig` 是唯一跨 ABI 结构体；新增可选字段只在尾部追加、不改 `MIN` | `docs/design.md:454`「`NexCoreConfig` 是唯一的跨 ABI 结构体」；`:462`「仅在尾部追加有默认值的可选字段除外（只更新 `NEXCORE_CONFIG_CURRENT_SIZE`）」 |
| F4 | 当前布局：`MIN=72`、`NATIVE_SIZE=120`、`EVAL_MODE_SIZE=124`、`INTERRUPT_SIZE=144`、`CURRENT=144` | `abi/abi-layout.txt:40-45`；`src/NexCore.h.ct:135-149` |
| F5 | 发布头的前置声明只允许一行 | `abi/expected-forward-decls.txt:1` 仅 `struct NexCoreConfig;`；`docs/design.md:504`「任何新增行都视为 ABI 表面变更，必须在评审中显式确认」 |
| F6 | 新增统计键从 `19` 起顺序追加、`NEXT_FREE` 顺延；已分配未实现返回 `NX_E_INTERNAL` 且不写 `*out` | `docs/design.md:639`；`docs/design.md:638` |
| F7 | 返回码 `0..99` 保留给 ABI，新增从 `12` 起；协议层错误码是字符串，**与 ABI 码没有数值映射** | `docs/design.md:604`「协议层错误码是字符串…与这里没有数值映射」 |
| F8 | ABI 尾字段是双向兼容（旧库忽略尾部、新库对缺失字段取默认值）；**新增导出函数不具备这种降级能力** | `docs/design.md:458`「两个方向都要成立…」 |

### 1.2 `Shell.spawn` 现状

| # | 事实 | 出处 |
| --- | --- | --- |
| F9 | 同步 MVP：`popen(cmd,"r")` 读满 stdout 后 `pclose`，返回 `{"code":N,"stdout":"..."}` | `hosts/shell/main.c:179-218`（`:190` `p = popen(cmd, "r");`） |
| F10 | 通过 PH-10 的 Provider 尾字段注册，不新增符号 | `hosts/shell/main.c:396-400`（`cfg.native_call = on_native;` / `cfg.native_names = NAMES;`）；`:329` `NAMES[] = "[\"Shell.print\",\"Shell.spawn\"]"` |
| F11 | PH-13 的定面结论与遗留登记 | `docs/phases/PH-13/card.md:64-69`「**不新增任何 ABI 符号**…异步/流式子进程需要新 ABI（事件回投），**本 phase 不做**，登记 K-6」；`:154` |
| F12 | Provider 安装脚本在 bootstrap **之前**执行，因此宿主 bootstrap 可以覆盖已挂载的 `Shell.*` | `src/js_backend/js_backend_quickjs.ct:291-306`（`install_natives` 在 `bootstrap` 求值之前）；`:58-66` 挂载模板 `o[p[p.length-1]]=function(){return __nx_native(n,JSON.stringify([...]))}` |

### 1.3 中断与错误面现状

| # | 事实 | 出处 |
| --- | --- | --- |
| F13 | 中断原因已在引擎侧记录：`timeout` 与 `interrupted` 只差一处文案改写 | `src/js_backend/js_backend_quickjs.ct:430-441`（`if (st->interrupt_reason == 2)` → `TIMEOUT_MSG`） |
| F14 | 出站 `$error` 的 `code` 被硬编码为 `E_JS_EXCEPTION`，`LAST_ERROR` 一律 `NX_E_INTERNAL` | `src/NexCore.ct:224-226`（`PREFIX = "{\"code\":\"E_JS_EXCEPTION\"…"`）；`:306-307`（`impl->last_error = NX_E_INTERNAL; push_script_error(...)`） |
| F15 | K-3 的结论是「不新增 ABI 错误码」，复活条件三条 | `docs/reviews/nexcore-ph12-k3-abi-error-code-review.md:34-38`；`:42-46` |
| F16 | 协议域已有 `E_TIMEOUT`(-32001)、`E_CANCELLED`(-32002)、`E_JS_RUNAWAY`(-32008)，无 `E_INTERRUPTED` | `docs/protocol.md:103-110` |
| F17 | 协议规则：调用方**按 `code` 分支**，`message` 只用于日志/展示；新增错误码不升协议版本，只更新 schema hash | `docs/protocol.md:122`；`:123`；`:283` |

### 1.4 异步通路现状（本轮最关键的一组事实）

| # | 事实 | 出处 |
| --- | --- | --- |
| F18 | 库里没有线程、没有互斥量、没有条件变量 | `src/` 全目录检索 `pthread_create|pthread_mutex|cond_signal|cond_wait` 零命中 |
| F19 | inbox 队列 `push`/`pop` 都是**无锁**读改写（`r->count` 直接自增/前移） | `src/queue/queue.ct:38-53`（`push`）、`:55-66`（`pop`） |
| F20 | `nexcore_drain` 忽略 `max_wait_ms`，注释自陈「同步阶段…空队列立即返回」 | `src/NexCore.ct:354-361`；`docs/tutorial.md:396`「恒返回 `NX_OK` 且 `*out_count=0`，忽略 `max_wait_ms`」 |
| F21 | inbox 的每条消息被当作 **JS 源码**求值，不是 JSON 信封 | `src/NexCore.ct:299`（`js_backend::Backend.eval(impl->js, msg, len, …)`） |
| F22 | 文档侧承诺的是 MPSC + 阻塞 drain + 信封 | `docs/async-threading.md:45`、`:54`（inbox「任意线程」）、`:60`（「inbox 是 MPSC…锁外 `cond_signal`」）、`:62`（`nexcore_drain(-1)` 是唯一无限等待） |
| F23 | 验收侧早已把「多线程 drain」「8 线程 × 10k send」「drain(-1) 阻塞」写成判据 | `docs/acceptance.md:70`（ACC-A-08）、`:94`（ACC-C-02）、`:100`（ACC-C-08） |
| F24 | 宿主是「send 一次 → drain 一次」的同步流程，没有 drain 循环 | `hosts/shell/main.c:419-434` |
| F25 | 异步 `spawn` 的 Promise 语义必须排在微任务泵之后 | `docs/reviews/nexcore-shell-host-ruling.md:272`、`:275`；泵已落地，见 `tools/check.sh:629-644`（⑮） |

【推断】F18–F21 合起来的含义：今天从第二个线程调 `nexcore_send` 与 drain 线程的 `pump_inbox` 并发，是**数据竞争**（UB），不是「返回 `NX_E_THREAD`」。F22/F23 说明这不是「设计上不允许」，而是**契约已写、实现未做**。

---

## 2. K-6：异步/流式 `Shell.spawn` 的 ABI 面

### 2.1 先拆需求：三件事被混成了一件

| 子需求 | 真实含义 | 谁必须拥有 |
| --- | --- | --- |
| A 异步 | 求值不被子进程阻塞；JS 拿到 Promise | 宿主（它已在 `native_call` 里，天然可起线程） |
| B 流式 | stdout 增量回投，不必等进程结束 | 宿主产生分片；库只负责搬运 |
| C 取消/回收 | JS 或关闭流程能终止子进程 | 宿主（进程是它 fork 的） |

【推断】三件事的所有权都落在宿主，因此**没有一项要求库知道「进程」这个概念**。PH-13 卡 §3.4 的「需要新 ABI（事件回投）」把「需要一条回投通路」误读成了「需要新的 ABI 面」——回投通路已经存在，就是 `nexcore_send`（F1/F22）。

### 2.2 三个方案

| 方案 | 形态 | ABI 改动 | 主要代价 | 判定 |
| --- | --- | --- | --- | --- |
| **K6-A 零 ABI（推荐）** | 宿主 worker 线程读子进程 → `nexcore_send` 投事件 → drain 线程交给 JS → bootstrap 里的 Promise 包装 resolve | **0 符号、0 字段、0 回调** | 依赖 F19/F20/F21 三项既有缺口先补；过渡期回投内容是 JS 片段 | 推荐为 MVP |
| K6-B 尾字段「类型化事件入队」 | 新增 `NexCoreEventFn` 之类的尾字段，让宿主用 JSON 一次入队，避免 JS 源码 eval | +2 字段（`CURRENT_SIZE` 144→160） | 只省一次 eval，语义与 `nexcore_send` 重叠；仍要解决 F19/F20 | 不做 |
| K6-C 完整异步 ABI | 库拥有子进程：新增导出函数（如 `nexcore_spawn`/`nexcore_spawn_ctl`）+ 跨 ABI ops 表 | 7 符号→8/9；可能升 `NEXCORE_ABI_VERSION` | 见 §2.4；与 D4「宿主实现」冲突 | 不建议 |

### 2.3 K6-A 的语义与线程归属

| 环节 | 线程 | 语义 | 依据 |
| --- | --- | --- | --- |
| `Shell.spawn(cmd)` | JS 线程（现实现为 drain 调用线程） | 生成 `id`，登记 `pending[id]`，调 `__nx_native("Shell.spawn.start",[id,cmd])` 后**立即返回 Promise** | F12（bootstrap 在挂载之后，可覆盖）、F21 |
| 启动子进程 | 宿主 worker | fork/exec（或 `popen`），把 `id` 与 pid 记入宿主表 | F9 |
| stdout 分片 | 宿主 worker | 每片调用 `nexcore_send(h, …)`；被 `NX_E_BACKPRESSURE` 时按策略合并或丢弃并计数 | F22（inbox 任意线程）、F19（需先补锁） |
| 投递 | drain 线程 | `pump_inbox` 取到片段，交给 JS 侧待定回调 | F21 |
| resolve / reject | JS 线程 | 收到 `exit` 片段后 resolve `{code,stdout}`；收到 `error` 片段 reject | — |
| 取消 | JS 线程 → 宿主 | JS 调 `Shell.spawn.cancel(id)`（同一条 Provider 通路），宿主 kill 并回收 | F10 |
| 关闭回收 | 宿主主线程 | `nexcore_shutdown` 前 kill 所有子进程并 join worker；**free 之后不得再 send** | `docs/design.md:659`（FREED 后调用是 UB） |

三条硬约束（【推断】，来自既有文档）：
1. `sink` 仍只在 drain 线程调用（`docs/async-threading.md:37`），流式片段不得从 worker 直接调 sink。
2. 事件可丢、响应不可丢（`docs/protocol.md:192`、`:276`）——stdout 片段属于事件，超容量必须计数，不能伪装成成功。
3. `free` 必须先 join worker（F8/`docs/design.md:659`），否则 UAF。

### 2.4 若走 K6-C：具体影响（备查，不推荐）

| 维度 | 具体变化 |
| --- | --- |
| 导出符号 | 至少 +1（生命周期控制）到 +2（启动 + 控制）；`tools/check.sh:267` 的 `ABI_SYMS` 与 ACC-A-02 白名单同提交改 |
| 跨 ABI 结构体 | 若用 ops 表，则 `NexCoreConfig` 不再是唯一结构体，直接撞 `docs/design.md:454` 与 ACC-A-01（发布头只允许 `NexCoreConfig`） |
| 布局 golden | `abi/abi-layout.txt` 新增 struct/typedef 段；`CURRENT_SIZE` 若走尾字段则 144→160 |
| 前置声明 | `abi/expected-forward-decls.txt` 必须新增行 → 按 `docs/design.md:504` 属「必须显式确认」的表面变更 |
| 版本 | 新符号无法优雅降级（F8）：旧库遇到新宿主是**加载期缺符号**，不是 D-18 那种「尾部被忽略」。故须升 `NEXCORE_ABI_VERSION` 并同步 Swift/Kotlin 绑定 |
| 迁移 | 移动端要重新生成绑定、重新验收 ACC-A-05/06/07；本仓无绑定代码可同步，成本落在宿主项目 |
| 定性 | 这不是「加一个回调」，而是把进程所有权搬进库——与 `docs/phases/PH-13/card.md:66` 的「由宿主在自己的进程里 fork/exec」正相反 |

### 2.5 MVP 与完整形态的边界

| | MVP（K6-A） | 完整形态（K6-C，若将来需要） |
| --- | --- | --- |
| 异步 | 有（Promise） | 有 |
| 流式 | 有（分片事件，经 JSON/JS 边界） | 有（库内零拷贝回调） |
| 取消 | 有（宿主 kill） | 有（库内统一调度） |
| stdin / PTY / 进程组 | 无 | 有 |
| 跨平台 | macOS 先验 | 需 Windows/Android 各自的进程模型 |
| ABI 面 | 0 | 新符号 + ops 表 + ABI v2 |

**升级判据（建议，需裁决）**：出现下列任一情况才重开 K6-C——
1. 需要**不经过 JS** 直接消费 stdout（例如宿主侧日志管道），JSON 边界成为瓶颈；
2. 需要 PTY / stdin 交互 / 进程组信号转发；
3. 实测分片路径的 drain 线程占用成为可观测瓶颈（给出仪器与阈值后再定，本轮不预设数字）。

---

## 3. K-7：中断/超时错误的可判定性

### 3.1 先划清 K-3 的边界

【事实】K-3 冻结的是 **ABI 整数码**（`NX_E_*`），并且明说「分类信息是否已经可得？**是**：`$error.code` 字符串已在协议里」（`docs/reviews/nexcore-ph12-k3-abi-error-code-review.md:28`）。而 `docs/design.md:604` 明确两套码「没有数值映射」。

【推断】K-7 要的是「宿主能区分超时/中断/普通内部错」。这件事在协议域就能做完，**不触发 K-3 的复活条件**（`:42-46` 三条：移动端按类别审计、需要行号/栈、`$error` 改响应）。因此 K-7 不需要重开 K-3，也不需要 ABI 新码。

### 3.2 四个备选

| 方案 | 做法 | ABI | 协议 | 验收文档 | 代价 |
| --- | --- | --- | --- | --- | --- |
| **K7-A 协议码分流（推荐）** | 预算 → `E_TIMEOUT`（已存在）；宿主中断 → 新增 `E_INTERRUPTED`(-32015)；`code` 由中断原因决定 | 0 | +1 个错误码 | ACC-B-02「19 个」→20 | 库内要把 `interrupt_reason` 透到出站；改两张文档 |
| K7-A′ 可选字段分流 | `code` 仍 `E_JS_EXCEPTION`，`$error` 增可选 `reason:"timeout"\|"interrupted"` | 0 | +1 可选字段 | 不改（19 码不变） | 违背 `docs/protocol.md:122`「调用方按 `code` 分支」；宿主多一条分支维度 |
| K7-B 统计键 20 | 新增 `NX_STAT_INTERRUPT_REASON`（0/1/2），同步可读 | 0（枚举追加） | 0 | ACC-A-12 现断言 `20 → NX_E_INVALID_ARGUMENT`，必须改 | golden 必 diff；`NEXT_FREE` 19→20；只记最近一次，多消息交叠不可靠 |
| K7-C 尾字段错误回调 | 新增 `NexCoreErrorFn` 尾字段，库在失败时直接回调宿主 | +2 字段 | 0 | 需补 ACC | 与 `$error` 形成两套真相；回调线程/重入约束要重写一遍 |
| K7-D 维持现状 | 宿主解析 `message` 子串 | 0 | 0 | 0 | `message` 是展示文案，PH-13 已证明它只是「恰好」可区分（F13），随时被措辞改动打破 |

### 3.3 推荐与理由

**推荐 K7-A**：

1. 零 ABI 变更，`abi-layout.txt`、7 符号、`CURRENT_SIZE` 全不动（F1–F5）。
2. 与协议既有规则一致：调用方按 `code` 分支（`docs/protocol.md:122`），新增码只增不改、不升协议版本（`:123`、`:283`）。
3. 与 K-3 不冲突：K-3 管整数码，这里改字符串码（F7/F15/F17）。
4. 改动面最小且可判定：库内只需把已有的 `interrupt_reason`（F13）透出到 `push_script_error` 的 `code` 参数（F14）。

**K7-B 暂不做**：`nexcore_stat` 的语义是观测，不是控制；且它会把 `abi/abi-layout.txt` 与 ACC-A-12 一起拉动（F6），收益只覆盖「不想解析 `$error` 的调用方」。若将来出现具体消费者再开，判据：宿主在没有 sink 的前提下仍需分类（本仓当前所有宿主都有 sink，`hosts/shell/main.c:411`）。

**K7-C 不做**：复制通道 = 两套真相，且回调线程约束与 `log` 相同（`docs/design.md:580`），成本高于收益。

**K7-D 只作为过渡**：若用户不批准动验收文档，维持现状并在 CHANGELOG 写明「`message` 不是契约」，同时把 K-7 保持挂账——不得记为已关闭。

### 3.4 实现切点（供后续 phase 卡引用，不在本评审实施）

| 层 | 位置 | 要改什么 |
| --- | --- | --- |
| 后端接口 | `src/js_backend/js_backend.h.ct:37-39` | `eval` 增加一个出参（例如 `int32_t *err_kind`，0=普通/1=预算/2=宿主中断）；两档后端同签名 |
| QuickJS 后端 | `src/js_backend/js_backend_quickjs.ct:430-441` | 已有 `interrupt_reason`，改为同时写 `err_kind`，文案改写逻辑保留 |
| 桥接层 | `src/NexCore.ct:224-226`、`:306-307` | `push_script_error` 接受 `code` 形参，按 `err_kind` 选 `E_TIMEOUT`/`E_INTERRUPTED`/`E_JS_EXCEPTION` |
| 文档 | `docs/protocol.md:96-116`；`docs/acceptance.md:81` | 增 `E_INTERRUPTED`；ACC-B-02 的「19 个」改「20 个」 |
| 不改 | `src/NexCore.h.ct`、`abi/*` | 零变更 |

---

## 4. 影响汇总

| 维度 | K-6（推荐 K6-A） | K-7（推荐 K7-A） |
| --- | --- | --- |
| 7 符号冻结 | 不动 | 不动 |
| `abi-layout.txt` | 不动 | 不动 |
| `expected-forward-decls.txt` | 不动 | 不动 |
| 协议文档 | 需要补「事件回投」一节（可选，见 §5.3 Q3） | 增 1 个错误码 |
| 验收文档 | 不需要新增条目；ACC-A-08/ACC-C-02/ACC-C-08 是既有条目，属「兑现」 | ACC-B-02 计数 +1 |
| 跨端绑定 | 不动 | 不动 |
| 版本 | 若开 PH-15 收口 → MINOR=15 | 若开 PH-14 收口 → MINOR=14；若只按 fix 处置 → PATCH +1 |
| 迁移 | 宿主需自管 worker 与子进程回收 | 宿主按 `code` 分支，旧宿主无感（仍见 `E_JS_EXCEPTION` 以外的码时按未知码处理即可） |

---

## 5. 结论与任务切分

### 5.1 是否开新 phase

| 项 | 建议 | 理由 |
| --- | --- | --- |
| K-7 | **开 PH-14** | 要改协议与验收两个契约文档，不是单点修复；三件任务可独立验收 |
| K-6 | **开 PH-15，但先补两项既有缺口** | 异步能力的真实前置是 F19（无锁）与 F20（drain 不阻塞），二者都已在 acceptance 里有判据，不是 ABI 问题 |
| K-6 的 ABI 面 | **不开评审、不立 phase** | 推荐方案零 ABI；K6-C 需先推翻 D4 与 `docs/design.md:454`，属另一层决策 |

### 5.2 任务切分（每项一个交付物 + 一条验收命令）

**PH-14（K-7）**

| task-id | deliverable | verify（单条命令口径） | 红证思路 | effort |
| --- | --- | --- | --- | --- |
| PH14-T1 | `eval` 增 `err_kind` 出参（两档后端）+ `push_script_error` 按原因选 `code` | `tests/interrupt.c` 输出 `INTERRUPT OK`，且 A 段 payload 含 `"code":"E_TIMEOUT"`、B 段含 `"code":"E_INTERRUPTED"` | 现行两条均 `"code":"E_JS_EXCEPTION"`（PH-13 卡 §9 E1/E2 原文可作 baseline 输出） | M |
| PH14-T2 | `docs/protocol.md` §4 增 `E_INTERRUPTED`；`docs/acceptance.md` ACC-B-02 计数 19→20 | `grep -c 'E_INTERRUPTED' docs/protocol.md` == 1 且 ACC-B-02 行含「20 个」；`bash tools/check.sh` 两档 `ALL CHECKS PASSED` | 纯文档任务，按 6.6 T-7 记 `no-test: 文档同步` 并由评审签字 | S |
| PH14-T3（可选） | 统计键 20 `NX_STAT_INTERRUPT_REASON` + `NEXT_FREE` 20→21 + golden + ACC-A-12 | `tests/stat_keys.c` 对 20 返回 `NX_OK` 且值 ∈{0,1,2}、对 21 返回 `NX_E_INVALID_ARGUMENT` | 现行 20 → `NX_E_INVALID_ARGUMENT`（`docs/acceptance.md:74`） | M |

**PH-15（K-6，推荐配置）**

| task-id | deliverable | verify（单条命令口径） | 红证思路 | effort |
| --- | --- | --- | --- | --- |
| PH15-T1 | inbox 线程安全：`src/queue` 加互斥量，`nexcore_send` 锁外唤醒 | `tests/queue_mpsc.c`：8 线程 × 10k send + 并发 drain，接收 + 拒绝 == 80000 且无重复；`-fsanitize=thread` 零报告 | 现实现无锁（F19），TSAN 报 data race | M |
| PH15-T2 | `nexcore_drain` 尊重 `max_wait_ms`（-1/0/>0），`shutdown` 唤醒 | `tests/drain_block.c`：空队列 `drain(-1)` 阻塞 ≥200 ms；另一线程 `send` 后 ≤100 ms 返回且 `*out_count==1` | 现行立即返回、`*out_count==0`（F20） | M |
| PH15-T3 | 宿主异步 spawn：worker + bootstrap Promise 包装 + shutdown 回收 | `bash tools/check-host.sh`：`nex -e '…Shell.spawn("sleep 0.2; echo hi").then(…)'` 输出顺序为 `tick` 在 `hi` 之前（证明求值未被阻塞） | 现行 `Shell.spawn` 返回普通对象，`.then` 为 undefined → `TypeError` | M |

【推断】T1/T2 单独拿出来也能闭合 ACC-A-08/ACC-C-02/ACC-C-08 三项既有判据；如果用户希望先缩小风险面，可以把它们作为 PH-15 的前两片，T3 随后。

### 5.3 需要用户裁决（5 条）

| # | 裁决点 | 选项 A | 选项 B | 本评审倾向 |
| --- | --- | --- | --- | --- |
| Q1 | K-7 的分流机制 | 新增协议码 `E_INTERRUPTED`（改 ACC-B-02 计数） | `$error` 加可选 `reason` 字段（不动验收计数） | A：与「按 `code` 分支」的既有规则一致 |
| Q2 | K-6 是否接受「零 ABI」的 MVP 定义 | 接受，按 K6-A 排 PH-15 | 不接受，要求先定完整 ABI 面（K6-C） | A；B 需先翻 D4 与单一结构体约束 |
| Q3 | 过渡期回投载体 | 允许宿主 `nexcore_send` 发 JS 片段 | 必须先补协议 JSON 信封分发（P2 范围） | B 更干净但更慢；若选 A，必须在 CHANGELOG 标注为非契约 |
| Q4 | F19/F20 的处置 | 作为 PH-15 前两片 | 作为两个独立缺陷先立案（PATCH） | A（它们没有「行为回归」，是契约未实现） |
| Q5 | 是否新增统计键 20（PH14-T3） | 做 | 不做，等具体消费者 | 不做（K-3 同一理由：不为假想需求设计） |

### 5.4 本评审明确不做

- 不改任何实现、`tools/check.sh`、`docs/roadmap.md`、`docs/acceptance.md`。
- 不提交 phase 卡；PH-14/PH-15 的卡面与退出判据由对应 phase 立项时另写。
- 不 push、不打 tag、不碰其他会话在改的文件。

---

## 6. 未核实与风险

| # | 项 | 状态 |
| --- | --- | --- |
| U1 | 分片 JSON 路径的实际吞吐/延迟 | 未核实：本轮不构建，未取数 |
| U2 | Windows/Android 的 `popen`/`fork` 语义 | 未核实：K6-A 只在 macOS 有先例（F9） |
| U3 | 移动端 transport 是否会高频并发 `nexcore_send` | 未核实：仓内无绑定代码 |
| U4 | 协议信封分发（F21 与 `docs/protocol.md` 的差距）的排期 | 未核实：`docs/roadmap.md` 未逐条列出 P2 子项 |
| R1 | 若 Q2 选 B（完整 ABI），`abi-layout.txt` 与 ACC-A-02/ACC-A-03 会同时变更，跨端绑定需重建 | 风险已列于 §2.4 |
| R2 | 若 Q1 选 B（可选字段），宿主会多一条分支维度，且 ACC-B-02 的「19 个码」与实际码表长期不一致 | 风险已列于 §3.2 |
