# PH-14 卡：K-7 中断错误面分流（协议码）

> 状态：**已冻结（2026-11-16）**，待开工。`version-minor`：**14**。
> 上游：[PH-13 遗留 ABI 面评审](../../reviews/nexcore-ph13-k6-k7-abi-surface-review.md) §0/§3/§5.2、[PH-13 卡](../PH-13/card.md) §6/§10 K-7、[protocol.md](../../protocol.md) §4、[acceptance.md](../../acceptance.md) ACC-B-02。
> 禁止改动：`docs/roadmap.md`、`abi/*`、`src/NexCore.h.ct`、`.ai-engineering`、7 个导出符号、`NEXCORE_CONFIG_CURRENT_SIZE`。

---

## 1. 归属与顺序

| 顺序 | 交付物 | 归属 | 依据 |
| --- | --- | --- | --- |
| 1 | 引擎中断原因透出到后端接口（`err_kind` 出参） | 库层（`src/js_backend/` 两档后端） | 原因已在引擎侧记录，只差一层透出 |
| 2 | `$error.code` 按原因分流 | 库层（`src/NexCore.ct`） | 协议字符串码，与 ABI 整数码无映射 |
| 3 | 协议码表与验收计数同步 | 文档层（`docs/protocol.md`、`docs/acceptance.md` 单行） | 新码只增不改，不升协议版本 |

T1 → T2 串行；T2 的验收不依赖 T1 的实现细节（只核对文档与计数）。

---

## 2. 不变量

| # | 不变量 | 核对 |
| --- | --- | --- |
| I-1 | 仍是 7 个导出函数；`abi/abi-layout.txt` 的枚举、结构体与布局逐字不变 | G-⑩、G-⑪ |
| I-2 | `NEXCORE_CONFIG_CURRENT_SIZE = 144` 不变；`abi/expected-forward-decls.txt` 仍只有 1 行 | G-⑪、G-⑫ |
| I-3 | 不新增任何 `NX_E_*` ABI 码；`NX_STAT_LAST_ERROR` 维持 `NX_E_INTERNAL(9)` | E3 |
| I-4 | 不新增统计键；`NX_STAT_NEXT_FREE` 仍为 19 | G-⑥ |
| I-5 | 协议版本不升；错误码表按“新码只增不改”追加 | G-⑬ 不因错误码表变化 |
| I-6 | 两档 rc=0；null 档 SKIP 仍为 3；⑮⑯⑰ 不回退 | E5 |
| I-7 | K-3 不复活：不加整数码、不改 `$error` 信封形状（仍为 `{code,message}`） | E3、E4 |

---

## 3. 设计定死项

### 3.1 D1 `err_kind` 出参，不改 `message` 作为契约

后端接口 `eval` 追加一个出参 `int32_t *err_kind`，两档同签名：

| 值 | 含义 | 出站 `code` |
| --- | --- | --- |
| 0 | 普通求值失败 | `E_JS_EXCEPTION` |
| 1 | 预算超时（`default_timeout_ms`） | `E_TIMEOUT` |
| 2 | 宿主 `interrupt_call` 中断 | `E_INTERRUPTED` |

- QuickJS 档由已有的 `interrupt_reason` 写入 `err_kind`（当前 1=宿主、2=预算，映射到上表 2/1）。
- 空后端（`src/js_backend/js_backend.ct`）保持“恒失败、不写 `*out`”，并把 `*err_kind` 写 0。
- `message` 文案（`timeout: …` / `InternalError: interrupted`）保留，只用于日志/展示；**调用方按 `code` 分支**（protocol §4 规则）。

### 3.2 D2 `E_INTERRUPTED` 的取值与落点

- 新增协议码 `E_INTERRUPTED`，数值 `-32015`，可重试“否”，含义“宿主主动中断当前求值”。
- 不复用 `-32011` 空位：protocol §4 明示错误码数值不复用，避免旧日志误读。
- `push_script_error` 接受 `code` 形参，按 D1 的选择拼接前缀；JSON 转义逻辑不变。
- `LAST_ERROR` 仍写 `NX_E_INTERNAL`：K-7 只解决协议侧可判定，不触发 K-3 的复活条件。

### 3.3 D3 文档同步边界

- `docs/protocol.md` §4 只追加一行错误码，不改表头、不改既有 19 行的数值与语义。
- `docs/acceptance.md` **只允许**修改 ACC-B-02 判据里的计数“19 个”→“20 个”，不得改该条方法/阈值，不新增验收条目。
- 不改 `docs/roadmap.md`；不改 `abi/schema-hash.txt`、`tests/fixture.schema.yaml` 与 `tools/schema_hash.py`。

### 3.4 D4 不做统计键 20

不新增 `NX_STAT_INTERRUPT_REASON`，`NEXT_FREE` 维持 19。理由见 §9 裁定 4。

---

## 4. 任务清单

| task-id | deliverable | verify | commit | rollback | effort | 归属 | no-test |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PH14-T1 | `eval` 增 `err_kind` 出参（`js_backend.h.ct`、`js_backend.ct`、`js_backend_quickjs.ct`）+ `push_script_error` 按原因选 `code`（`src/NexCore.ct`）+ `tests/interrupt.c` 断言两条 payload 的 `code` | 按 E1 的编译命令产出 `build/bin/interrupt` 并运行：退出码 0；A 段 payload 含 `"code":"E_TIMEOUT"`，B 段含 `"code":"E_INTERRUPTED"`；两段 `last_error=9` | `test(interrupt): PH14-T1 红` + `feat(error): PH14-T1 中断错误面分流` | 成对 `git revert`；单独回退不改变 ABI 与既有验收 | M | 库层 | — |
| PH14-T2 | `docs/protocol.md` §4 追加 `E_INTERRUPTED(-32015)`；`docs/acceptance.md` ACC-B-02 计数 19→20 | `grep -c 'E_INTERRUPTED' docs/protocol.md` == 1 且该行含 `-32015`；`grep -q 'protocol.md §4 的 20 个错误码' docs/acceptance.md`；两档全量门禁 PASS | `docs(protocol): PH14-T2 增 E_INTERRUPTED 并同步 ACC-B-02 计数` | 成对 `git revert`；纯文档，无运行时依赖 | S | 文档层 | 文档同步（纯文档，无行为可红；按 6.6 T-7 由 REVIEW 在收口复核签字） |

**任务清单每项可独立验收**：每行都有自己的 `verify`、`commit`、`rollback` 与 `effort`；撤销任一任务不影响另一任务的判据与门禁。

**红证要求（PH14-T1）**：`test(...) 红` 必须是 `feat` 的父提交，红证写 `baseline: <sha>` + 命令 + 原始输出 + 退出码。现行两条 payload 均为 `"code":"E_JS_EXCEPTION"`（PH-13 卡 §9 E1/E2 原文可作基线对照），不得用当前代码复跑冒充。

---

## 5. 退出判据

| # | 判据 | 命令与阈值 | 对应 |
| --- | --- | --- | --- |
| E1 | 预算超时产出 `E_TIMEOUT` | `cc -std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration -I build/abi tests/interrupt.c build/impl/*.c experiments/third_party/quickjs/libquickjs.a -lm -lpthread -o build/bin/interrupt && ./build/bin/interrupt` 的 A 段：rc=0，payload 含 `"code":"E_TIMEOUT"`，`last_error=9`，进程 2s 内退出（夹具 `alarm(5)` 不得触发） | T1 |
| E2 | 宿主中断产出 `E_INTERRUPTED` | 同夹具 B 段：payload 含 `"code":"E_INTERRUPTED"`，`polls>=5`，`last_error=9` | T1 |
| E3 | ABI 错误面未被拉动 | `grep -c 'NX_E_' abi/abi-layout.txt` == 11（枚举仍为 1..11）；`grep -c 'NX_E_INTERRUPTED' abi/abi-layout.txt` == 0；`LAST_ERROR` 两段均为 9 | T1 |
| E4 | 文档契约一致 | `grep -c 'E_INTERRUPTED' docs/protocol.md` == 1 且该行含 `-32015`；`grep -q 'protocol.md §4 的 20 个错误码' docs/acceptance.md`；`git diff --name-only` 的文档集合只有上述两文件 | T2 |
| E5 | 全量门禁 | `bash tools/check.sh`（null）与 `NEXCORE_JS=quickjs bash tools/check.sh` 均 `ALL CHECKS PASSED`，null 档 SKIP=3；`bash tools/check-host.sh` 输出 `HOST OK` | 全部 |
| E6 | ABI 面未破 | G-⑩ 仍报 7 个 `nexcore_*`；G-⑪ 布局 golden 零差异；G-⑫ 前置声明仍 1 行；`CURRENT_SIZE=144` | T1 |

**UNVERIFIED 规则**：Swift/Kotlin 绑定不在本仓，协议新码的跨端消费无法在本机验证；登记为环境缺口，不记通过。Windows/Android 无进程/信号语义验证，同上。

---

## 6. 遗留项（预期）

| # | 项 | 处置 |
| --- | --- | --- |
| K-7 | 中断/超时在协议侧可判定 | 本 phase 收口后关闭；K-3 不复活 |
| K-7a | `message` 仍不是契约，只是兼容文案 | 不关闭、不承诺；协议规则已写明按 `code` 分支 |
| K-7b | 统计键 20（`NX_STAT_INTERRUPT_REASON`） | 不做，等具体消费者（评审 Q5） |
| K-7c | 移动端对 `E_INTERRUPTED` 的消费 | 环境缺口，UNVERIFIED |

---

## 7. 非目标

- 不新增/删除/改语义任何 `NX_E_*` ABI 整数码；不动 `NX_STAT_LAST_ERROR`。
- 不新增统计键、不改 `NEXT_FREE`、不改 `abi/*` 与发布头。
- 不改 `$error` 信封形状；不加尾字段错误回调（K7-C）。
- 不改 `tools/check.sh` 的既有编号与判据；不改 `docs/roadmap.md`。
- 不做异步 spawn（PH-15）与任何线程模型改动。

---

## 8. 收口动作

| 项 | 动作 | 判据 |
| --- | --- | --- |
| 版本 | `docs/README.md` 状态行 `v0.13.0` → `v0.14.0`（6.7 V-1，`MINOR=14`、PATCH 归零） | 与收口提交同提交；`g4-version` 四项核对通过 |
| 收口 tag / push | 按当前项目授权**不执行**（与 PH-13 同口径） | 补跑命令：`git tag -a v0.14.0 -m "PH-14 <sha>" <sha> && git push origin v0.14.0` |
| 证据 | 收口时在卡内补 E1–E6 原始输出与红证表 | 每条带命令、rc、原文摘要 |

---

## 9. 变更记录（battle 裁定，2026-11-16）

| # | 冲突/待裁点 | 裁定 | 理由 |
| --- | --- | --- | --- |
| 1 | 评审 T2 要改 `docs/acceptance.md` 的 ACC-B-02 计数，但 PH-13 卡与项目惯例写“不改 acceptance” | **允许 PH-14-T2 只改 ACC-B-02 的“19 个”→“20 个”单行**，其余 acceptance 与 roadmap 不动 | 错误码表与验收计数是同一契约；不同步会形成 6.7 V-6 类“文档与实现不一致”。PH-13 卡的禁令只约束 PH-13 |
| 2 | 评审 F17 说“新增错误码只更新 schema hash”，但本仓 schema hash 来自 `tests/fixture.schema.yaml` | **不改 schema hash、fixture、`abi/schema-hash.txt` 与 G-⑬** | 错误码表不在 schema 字节内；强行改 hash 会制造无来源的 golden 漂移。协议版本同样不升 |
| 3 | 用 `message` 分流还是用 `code` 分流 | **`code` 分流**；`message` 仅保留兼容文案 | protocol §4 明示“调用方按 `code` 分支；`message` 只用于日志/展示”；K7-A′ 会让宿主多一条分支维度 |
| 4 | 评审 Q5 的统计键 20 是否顺手做 | **不做**，登记 K-7b | 无具体消费者；会拉动布局 golden 与 ACC-A-12，违背 K-3“不为假想需求设计”的同一理由 |
| 5 | T1 是否新增 `check.sh` 编号 | **不新增**，复用 `tests/interrupt.c` 既有夹具与退出码 | 不改门禁编号与既有判据（PH-13 §7）；既有 E1/E2 基线可直接作红证对照 |
| 6 | `E_INTERRUPTED` 用 `-32015` 还是回填空位 `-32011` | **`-32015`** | protocol §4 明确“错误码数值不复用，避免旧日志误读” |
| 7 | PH-14 与 PH-15 的启动顺序 | **PH-14 先开工** | 零 ABI、改动面最小、可在单测内闭环；PH-15 的前置是线程/队列缺口，与本卡无依赖 |
