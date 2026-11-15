# PH-11 卡：shell 宿主阶段 1（脚本入口 + 错误面 + `nex` MVP）

> 状态：**已冻结（2026-11-15）**，开工。
> `version-minor`：**11**（收口时产品版本 MINOR 必须为 11）。
> 上游：[nexcore-shell-host-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md) §5/§6.2、[nexcore-ph11-stage1-battle.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-ph11-stage1-battle.md)（Q1/Q2 裁定）。
> 前置：PH-10 已收口（E1–E7 全 PASS），入口条件见 PH-10 卡 §10。
> 禁止改动：`docs/roadmap.md`、`docs/acceptance.md`、`.ai-engineering`。

---

## 1. 归属（先划清，避免把库层工作记到宿主名下）

| 交付物 | 归属 | 依据 | 谁受益 |
| --- | --- | --- | --- |
| 程序模式求值（多语句 / `#!`） | **库层**（`NexCoreConfig` 尾字段 + `src/js_backend/`） | 求值语义在引擎适配层 | 移动端：同一 handle 可选脚本模式，不必自建包装 |
| 错误面（`$error` 通知 + `LAST_ERROR`） | **库层**（`src/NexCore.ct`） | 出站通道在库内 | 移动端：异常文案不再被库内 free 掉 |
| `nex` 解释器 MVP | **宿主**（`hosts/shell/main.c`） | 只消费公开 ABI | 仅本地开发工具 |
| `Shell.print` 注入 | **宿主侧配置**（用 PH-10 D2 的尾字段） | 库只提供注册入口 | 移动端同样可用该入口 |
| 宿主用例 | **门禁层**（新增独立 `tools/check-host.sh`） | 不新增检查编号、不并入 `check.sh` 绿线 | — |

**结论**：本 phase 有两项库层任务（T1、T2），三项宿主/门禁任务（T3–T5）。前两项即使没有 CLI 也要做。

---

## 2. 不变量

| # | 不变量 | 核对方式 |
| --- | --- | --- |
| I-1 | 导出 ABI 仍是 **7 个**函数 | ⑩ 仍报 7 |
| I-2 | 跨 ABI 结构体仍只有 `NexCoreConfig` | ①b/①c 不变 |
| I-3 | `eval_mode` 尾部追加，`NEXCORE_CONFIG_MIN_SIZE` 不变 | ⑫ `<MIN` 仍拒绝 |
| I-4 | `NEXCORE_ABI_VERSION` 不升 | 头文件仍为 1 |
| I-5 | 两档 rc=0；null 档 SKIP 仍为 3 | 两档 `ALL CHECKS PASSED` |
| I-6 | 表达式模式行为不变（`{id:1}` 仍是对象） | ⑨/smoke + PH-10 ⑮⑯⑰ 不回归 |
| I-7 | 不改 `acceptance.md` / `roadmap.md` | `git diff --name-only` |

---

## 3. 设计定死项

### 3.1 D1 程序模式（裁定 Q1 = 显式）

```c
/* src/NexCore.h.ct：尾部追加，默认 0 */
enum NxEvalMode { NEXCORE_EVAL_EXPRESSION = 0, NEXCORE_EVAL_PROGRAM = 1 };
/* NexCoreConfig 末尾 */
int32_t eval_mode;   /* 0/缺省 = 表达式（现行行为）；1 = 程序模式（多语句、#!） */
```

- 可用性：`struct_size >= offsetof(eval_mode) + sizeof(eval_mode)`；读不到按 0。
- `js_backend.eval` 增 `mode` 形参；`0` 时**保留**括号包裹，`1` 时按程序模式直接求值。
- `#!` 由引擎接受（实测），库不剥；只在卡与宿主文档写明。
- 结果仍是「完成值」的 JSON；程序模式无完成值（`undefined`）时返回 `null`，不报错。

### 3.2 D2 错误面（裁定 Q2 = `$error` 经 sink）

- `js_backend.eval` 失败时额外返回错误文案出参（内部接口，不是 ABI）。
- `NexCore.ct` 的 `pump_inbox`：eval 失败 → 推一条 `{"code":"E_JS_EXCEPTION","message":"<文案>"}` 进 outbox；`LAST_ERROR = NX_E_INTERNAL`。
- 一条消息最多一条 `$error`；`$error` 自身不再触发错误投递（无递归）。
- **不加 ABI 错误码**：`NX_E_JS_EXCEPTION` 列 PH-12 候选（跨端契约要同步 Swift/Kotlin）。

### 3.3 D3 宿主 `nex` MVP

- `hosts/shell/main.c`：只 `#include "NexCore.h"`；argv[1] 必填，其余报用法错。
- 流程：读文件 → `create`（`eval_mode = PROGRAM`、`native_names = ["Shell.print"]`）→ `send` → `drain` → 按 sink 内容打到 stdout/stderr → `shutdown`/`free`。
- exit code：`0` 成功；`1` 脚本错误（`LAST_ERROR != 0`）；`2` 用法/IO 失败。
- `Shell.print(...)` 回调把 `args[0]`（JSON 数组首元素）原样写 stdout，返回 `null`。
- 链接：`build/bin/libnexcore.a` 的**显式路径** + `libquickjs.a`（`.a` 不含引擎）。
- 产物 `build/bin/nex`，不安装到系统目录；shebang 文档写 `#!/usr/bin/env nex`。

### 3.4 D4 宿主门禁

- 新增 `tools/check-host.sh`（独立入口，不被 `check.sh` 调用，不占检查编号）。
- 依赖缺失时 rc=2 并打印缺什么（不许静默绿）。
- 三个正例 + 两个负例（见 E4/E5）。

---

## 4. 任务清单

| task-id | deliverable（一条） | verify | commit | rollback | effort | 归属 |
| --- | --- | --- | --- | --- | --- | --- |
| PH11-T1 | `eval_mode` 尾字段 + 程序模式求值 | `tests/eval_mode.c`：程序模式 `var a=1; a+1` → `2`、`#!` 脚本 → `3`；表达式模式 `{id:1}` 仍是对象 | `test(eval): PH11-T1 红` + `feat(eval): PH11-T1 …` | 成对 revert；golden 同 revert | M | 库层 |
| PH11-T2 | 错误面：`$error` 通知 + `LAST_ERROR` | `tests/error_surface.c`：`throw new Error("boom")` 后 sink 收到 1 条 `$error`，`code=="E_JS_EXCEPTION"` 且 message 含 `boom`；`LAST_ERROR != 0` | 同上 | 成对 revert | M | 库层 |
| PH11-T3 | `hosts/shell/main.c`（argv/读文件/ABI/exit code/`Shell.print`） | `bash tools/check-host.sh` 的三个正例（见 E4） | `test(host): PH11-T3 红` + `feat(host): PH11-T3 …` | 单提交 revert（仅宿主目录） | M | 宿主 |
| PH11-T4 | `tools/check-host.sh` 负例与 shebang 用例 | 两个负例：语法错 → rc=1 且 stderr 非空；用法错 → rc=2 | 同上 | 成对 revert | S | 门禁层 |
| PH11-T5 | 文档：`hosts/README.md` 补使用与 shebang 口径 | `grep -n "env nex" hosts/README.md` 命中；无未实现承诺 | `docs(hosts): PH11-T5 …`（`no-test:` 纯文档） | 单提交 revert | S | 文档层 |

**T-5 自检**：每项一个交付物；T1/T2 是库层，T3/T4 是宿主与门禁，T5 纯文档。
**T-7 例外**：仅 T5 为纯文档，其余四项要求「红证提交 → 实现提交」相邻。

---

## 5. 退出判据（逐条二值可判定）

| # | 判据 | 判定命令与阈值 | 对应 |
| --- | --- | --- | --- |
| E1 | 程序模式可用 | `tests/eval_mode.c` 输出 `PROGRAM OK`：`var a=1; a+1` → `2`；`#!/usr/bin/env nex\nvar b=2; b+1` → `3` | T1 |
| E2 | 表达式模式不回归 | 同一夹具：`{id:1}` 求值结果等于 `{"id":1}`；`1+1` → `2`；⑨/smoke/⑮⑯⑰ 仍 PASS | T1 |
| E3 | 错误面可观测 | `tests/error_surface.c` 输出 `ERROR OK`：`$error` 恰 1 条、`code=="E_JS_EXCEPTION"`、message 含 `boom`、`LAST_ERROR != 0` | T2 |
| E4 | 宿主正例 | `bash tools/check-host.sh` → `HOST OK`：①打印 `42` 且 rc=0；②`#!` 脚本直跑 rc=0；③`Shell.print("hi")` 输出 `hi` | T3/T4 |
| E5 | 宿主负例 | 同一脚本：语法错脚本 rc=1 且 stderr 非空；无参数 rc=2 | T4 |
| E6 | 门禁不污染库绿线 | `bash tools/check.sh` 与 `NEXCORE_JS=quickjs bash tools/check.sh` 仍 `ALL CHECKS PASSED`；`check-host.sh` 不在 `check.sh` 内被调用 | T4 |

**附加固定条**：任务清单每项可独立验收（6.6 T-6）。

**UNVERIFIED 规则**：缺 `libquickjs.a`、缺 `cc`、shebang 无法在无安装环境下验证时，写 `UNVERIFIED` + 原因 + 补跑命令，不计通过。

---

## 6. 红证要求

每项两提交，`baseline` 为 `test` 提交的父提交，红证贴原始输出与退出码；用实现后的工作树复跑不算红证。T5 纯文档可用 `no-test: 纯文档`，但须在提交正文写理由。

---

## 7. 非目标

- 不做自动模式探测（裁定 Q1 已否决，复活条件见 battle §3.1）。
- 不做 `-e`、`process.argv/env`、stdin、SIGINT（阶段 2）。
- 不做 `Shell.spawn` / 异步子进程（阶段 2/3，且受 D-05 预算约束）。
- 不新增 ABI 函数、不新增 ABI 错误码、不新增跨 ABI 结构体。
- 不做 `nex` 的安装、签名、包管理器分发（裁定 §7.3）。
- 不改 `check.sh` 的既有检查项与编号。

---

## 8. 环境缺口与收口动作（如实登记，不记通过）

| 项 | 事实 | 补跑命令 |
| --- | --- | --- |
| 仓库根 `build/` | 被 macOS 封存，所有实跑在 `/tmp/nx11` 副本进行 | `rsync -a --exclude build --exclude .git <repo>/ /tmp/nx11/` 后按 §5 跑 |
| `libquickjs.a` | 本地需先构建；缺则 `check.sh` rc=2 | `bash experiments/build.sh`（或复用 `/tmp/nx10/experiments/third_party/quickjs/libquickjs.a`） |
| `nex` 安装 | 本 phase 不安装到系统目录，shebang 用临时目录 + 绝对路径验证 | `PATH="$PWD/build/bin:$PATH" build/bin/nex script.nexjs` |
| 收口 tag / push | **本轮授权不做**；按 6.7 V-7 属收口动作缺口，登记待办 | `git tag -a v0.11.0 -m "PH-11 <收口 sha 前 7 位>" <sha> && git push origin v0.11.0` |
