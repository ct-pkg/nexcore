# PH-13 卡：求值中断与预算（K-4）+ `Shell.spawn` 的 ABI 面

> 状态：**已冻结（2026-11-15）**，开工。`version-minor`：**13**。
> 上游：PH-12 卡 §10 K-4、[nexcore-shell-host-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md) §6.2 阶段 3、[nexcore-ph12-k3-abi-error-code-review.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-ph12-k3-abi-error-code-review.md) §5。
> 禁止改动：`docs/roadmap.md`、`docs/acceptance.md`、`.ai-engineering`。

---

## 1. 归属与顺序

| 顺序 | 交付物 | 归属 | 依据 |
| --- | --- | --- | --- |
| 1 | 中断钩子 + 预算 | **库层**（`NexCoreConfig` 尾字段 + `js_backend`） | 引擎中断入口在适配层；移动端同样受益 |
| 2 | 宿主 SIGINT 接线 | **宿主层**（`hosts/shell/main.c`） | 只把信号标记喂给库的回调 |
| 3 | `Shell.spawn`（同步 MVP） | **宿主层**（Provider 回调内实现） | 不新增 ABI 符号 |

---

## 2. 不变量

| # | 不变量 | 核对 |
| --- | --- | --- |
| I-1 | 仍是 7 个导出函数；跨 ABI 结构体仍只有 `NexCoreConfig` | ⑩ ①b |
| I-2 | `abi_version` 不升；layout golden 只随尾字段移动 | ⑪ ⑫ |
| I-3 | 两档 rc=0；null 档 SKIP 仍为 3 | 两档 `ALL CHECKS PASSED` |
| I-4 | ⑮⑯⑰ 不回退 | 两档 PASS |
| I-5 | 不新增 ABI 错误码（K-3 结论） | `grep -c NX_E_JS_EXCEPTION abi/abi-layout.txt` == 0 |
| I-6 | `Shell.spawn` 不进 ABI | 导出符号仍 7 个 |

---

## 3. 设计定死项

### 3.1 D1 中断钩子：尾字段，不加函数

```c
/* src/NexCore.h.ct 尾部追加 */
typedef int32_t (*NexCoreInterruptFn)(void *user); /* 非 0 = 请求中断 */
/* NexCoreConfig 末尾 */
NexCoreInterruptFn interrupt_call;
void *interrupt_user;
```

- 可用性判据：`struct_size >= offsetof(interrupt_user) + sizeof(void *)`；读不到视为未提供。
- quickjs 档在 `eval` 期间安装 `JS_SetInterruptHandler`；回调里：
  1. 若 `interrupt_call != 0` 且其返回非 0 → 中断；
  2. 若 `default_timeout_ms > 0` 且 `elapsed > default_timeout_ms` → 中断（预算上限）。
- 中断后异常文案由 PH-11 的 T2 通道透出：`$error.message` 含 `interrupted` 或 `timeout`；`LAST_ERROR = NX_E_INTERNAL`（K-3：不加 ABI 码）。
- null 档忽略该字段（无引擎，无可中断的求值）。

**为什么不是新增导出函数**：`nexcore_set_interrupt()` 会破 7 符号冻结（ACC-A-02），且尾字段已被 D-18 制度化为唯一扩展方式。

### 3.2 D2 预算语义

- 预算 = `default_timeout_ms`，单位毫秒，从**单条消息 eval 开始**计时；`0` 表示不限。
- 超预算与宿主中断走同一条中断路径，只在文案上区分；不做分级降级。

### 3.3 D3 宿主 SIGINT 接线

- 宿主把现有 `g_sigint` 包成 `NexCoreInterruptFn` 传给 `interrupt_call`。
- 中断发生 → eval 失败 → 宿主按 `g_sigint` 优先返回 **130**；否则 1。
- 求值外收到 SIGINT 的语义不变（仍是 130）。

### 3.4 D4 `Shell.spawn` 的 ABI 面结论（先定面，再实现）

- **不新增任何 ABI 符号**：`Shell.spawn` 是宿主注册的 Provider（PH-10 D2 的 `native_call`），由宿主在自己的进程里 fork/exec。
- 语义（MVP）：`Shell.spawn(cmd)` 同步执行 `/bin/sh -c cmd`，返回 `{"code":<int>,"stdout":"..."}`；超时与非零退出都通过 `code` 表达，不抛异常（异常只用于「没法执行」）。
- 异步/流式子进程需要新 ABI（事件回投），**本 phase 不做**，登记 K-6。
- 判据：⑩ 仍报 7 个 `nexcore_*`；`abi/abi-layout.txt` 不出现 spawn 相关符号。

---

## 4. 任务清单

| task-id | deliverable | verify | commit | rollback | effort | 归属 |
| --- | --- | --- | --- | --- | --- | --- |
| PH13-T1 | 中断钩子 + 预算（尾字段 + 引擎回调） | `tests/interrupt.c`：A 预算中断、B 宿主回调中断，均在 alarm 内返回且 `$error` 文案可判 | `test(interrupt): PH13-T1 红` + `feat(interrupt): …` | 成对 revert + golden 回退 | M | 库层 |
| PH13-T2 | 宿主 SIGINT 接线 | `check-host.sh`：自旋脚本 + `kill -INT`，1.5s 内退出且 rc=130 | 同上 | 成对 revert | S | 宿主 |
| PH13-T3 | `Shell.spawn` 同步 MVP | `check-host.sh`：`Shell.spawn("echo hi")` 输出含 `hi`，`code=0`；`exit 3` → `code=3` | 同上 | 成对 revert | M | 宿主 |

---

## 5. 退出判据

| # | 判据 | 命令与阈值 | 对应 |
| --- | --- | --- | --- |
| E1 | 预算能中断求值 | `tests/interrupt.c` → `INTERRUPT OK`：`while(true){}` + 200ms 预算，`stat(JS_EXCEPTIONS) >= 1` 或 `LAST_ERROR != 0`，且 wall clock < 2s | T1 |
| E2 | 宿主回调能中断 | 同夹具：回调第 5 次返回 1 → 求值中止，`$error` 文案含 `interrupted` | T1 |
| E3 | Ctrl-C 真能停 | `check-host.sh`：`while(true){}` 脚本 100ms 后 `kill -INT`，1.5s 内进程退出且 rc=130 | T2 |
| E4 | `Shell.spawn` 可用 | `Shell.spawn("echo hi")` 的 `code==0` 且 stdout 含 `hi`；`exit 3` → `code==3` | T3 |
| E5 | ABI 面未破 | `⑩` 仍 7 个符号；`grep -c spawn abi/abi-layout.txt` == 0 | T3 |
| E6 | 全量门禁 | 两档 `ALL CHECKS PASSED`、SKIP=3、`HOST OK` | 全部 |

**UNVERIFIED 规则**：Windows/Android 的信号语义不在本机可验范围，登记为环境缺口，不记通过。

---

## 6. 遗留项（预期）

| # | 项 | 处置 |
| --- | --- | --- |
| K-6 | 异步/流式 `Shell.spawn` 需要事件回投的新 ABI | 挂账，另开 ABI 评审（先定面后实现） |
| K-7 | 中断后 `LAST_ERROR` 仍为 `NX_E_INTERNAL`，宿主无法与普通内部错区分 | 受 K-3 约束；复活条件见 K-3 评审 §5 |

---

## 7. 非目标

- 不做中断原因的结构化 ABI（K-3/K-7）。
- 不做进程组回收、信号转发、PTY。
- 不做异步 spawn、stdout 流式回调。
- 不改 `check.sh` 既有编号与既有判据。

---

## 8. 收口动作

| 项 | 事实 | 补跑命令 |
| --- | --- | --- |
| 仓库根 `build/` | 被封存，实跑在 `/tmp/nx13` | `rsync -a --exclude build --exclude .git <repo>/ /tmp/nx13/` |
| 收口 tag / push | 本轮授权不做 | `git tag -a v0.13.0 -m "PH-13 <sha>" <sha> && git push origin v0.13.0` |

---

## 9. 收口证据表（2026-11-15，`/tmp/nx13` 副本实跑）

| # | 判据 | 命令 | 原始输出摘要 | rc | 结论 |
| --- | --- | --- | --- | --- | --- |
| E1 | 预算能中断求值 | `tests/interrupt.c` 用例 A | `drain rc=0 delivered=1 last_error=9` / `payload={"code":"E_JS_EXCEPTION","message":"timeout: evaluation exceeded budget"}` | 0 | PASS |
| E2 | 宿主回调能中断 | 同夹具用例 B（回调第 5 次返回 1） | `payload={"code":"E_JS_EXCEPTION","message":"InternalError: interrupted"}` / `polls=5` / `INTERRUPT OK` | 0 | PASS |
| E3 | Ctrl-C 真能停 | `check-host.sh`：`while(true){}` + 100ms 后 `kill -INT` | 进程 1.5s 内退出，退出码 `130` | 0 | PASS |
| E3b | 预算在宿主侧可判 | `NEX_TIMEOUT_MS=200 nex while(true){}` | stderr 含 `timeout`，退出码 `1` | 0 | PASS |
| E4 | `Shell.spawn` 可用 | `Shell.spawn("echo hi").stdout.trim()` / `String(…"exit 3".code)` | `hi` / `3` | 0 | PASS |
| E5 | ABI 面未破 | ⑩ 与 `grep -c spawn abi/abi-layout.txt` | `⑩ libnexcore: PASS（仅 libc/pthread，7 个 nexcore_* 导出）` / `0` | 0 | PASS |
| E6 | 全量门禁 | 两档 `check.sh` + `check-host.sh` | `ALL CHECKS PASSED (quickjs)` / `(null)`，SKIP=3；`HOST OK` | 0 | PASS |

**红证记录**

| task | baseline | 命令 | 原始输出（节选） | rc |
| --- | --- | --- | --- | --- |
| PH13-T1 | `19053db` | 用 PH-12 的 ABI 头编译 `tests/interrupt.c` | `unknown type name 'NexCoreInterruptFn'` / `no member named 'interrupt_call'` | 1 |
| PH13-T2 | `11c422c` | `bash tools/check-host.sh`（接线前） | `check-host: FAIL 求值期间 SIGINT 未中断（进程 1.5s 后仍存活）` | 1 |
| PH13-T3 | `75f013a` | `bash tools/check-host.sh`（注册前） | `TypeError: not a function` / `FAIL Shell.spawn echo 退出码非 0` | 1 |

**实现期修正（如实记录）**：预算最初用 `clock()`（CPU 时间），实测不触发；改为 `clock_gettime(CLOCK_MONOTONIC)` 后通过。init 期 bootstrap 也受预算约束，故 `eval_start` 在 `init` 里初始化，否则 bootstrap 会被立即中断（`create rc=9`）。

---

## 10. 遗留项

| # | 项 | 状态 |
| --- | --- | --- |
| K-4 | 求值期间 SIGINT | **关闭**：库层中断钩子 + 宿主接线，E3 可判定 |
| K-6 | 异步/流式 `Shell.spawn`（stdout 流式、事件回投）需新 ABI | 挂账；需另开 ABI 评审，本 phase 只定「不进 ABI」这一面 |
| K-7 | 中断/超时的 `LAST_ERROR` 仍是 `NX_E_INTERNAL` | 受 K-3 约束；宿主用 `$error.message` 区分 |

---

## 11. 仓库根补跑记录（2026-11-16）

**为什么补跑**：§8 记「仓库根 `build/` 被封存，实跑在 `/tmp/nx13`」，§9 的 E1–E6 全部来自副本。本节把同一套命令搬回仓库根重跑一遍，使证据在仓库本体内可复现。**只补记录，不改 §5 判据、§9 既有证据、§6/§10 遗留项。**

**环境说明（如实记录）**

- 仓库根 `build/` 当时**不存在**（不残留任何产物），无需解封：`tools/check.sh` 自带 `rm -rf build/abi build/impl build/bin` + `mkdir -p`，直接跑即重建。
- 缺的是引擎依赖 `experiments/third_party/quickjs/libquickjs.a`（`.gitignore` 忽略，只在副本里有）。按 `check.sh` 的提示先跑 `bash experiments/build.sh` 重建（QuickJS 2025-09-13），rc=0。
- 原始输出全部归档于本卡同目录 `evidence/`，文件名前缀 `2026-11-16-`。

| # | 判据 | 命令 | rc | 关键原始输出（节选） | 与 §9 对照 |
| --- | --- | --- | --- | --- | --- |
| E1 | 预算能中断求值 | `./build/bin/interrupt`（用例 A，`default_timeout_ms=200`） | 0 | `drain rc=0 delivered=1 last_error=9 payload={"code":"E_JS_EXCEPTION","message":"timeout: evaluation exceeded budget"}` | 一致 |
| E2 | 宿主回调能中断 | 同夹具用例 B（回调第 5 次返回 1） | 0 | `payload={"code":"E_JS_EXCEPTION","message":"InternalError: interrupted"}` / `polls=5` / `INTERRUPT OK` | 一致 |
| E3 | Ctrl-C 真能停 | `./build/bin/nex forever.nexjs` + 100ms 后 `kill -INT` | 130（进程退出码） | `E3 kill-INT rc=130 elapsed_ms=7`；`check-host.sh` 的 1.5s 存活探针未触发 `FAIL` | 一致 |
| E3b | 预算在宿主侧可判 | `NEX_TIMEOUT_MS=200 ./build/bin/nex forever.nexjs` | 1 | stderr `{"code":"E_JS_EXCEPTION","message":"timeout: evaluation exceeded budget"}` | 一致 |
| E4 | `Shell.spawn` 可用 | `nex spawn.nexjs` / `nex spawn_code.nexjs` | 0 | `E4 spawn_stdout=hi` / `E4 spawn_code=3` | 一致 |
| E5 | ABI 面未破 | quickjs 档 `⑩` / `grep -c spawn abi/abi-layout.txt` | 0 | `⑩ libnexcore: PASS（仅 libc/pthread，7 个 nexcore_* 导出）` / `0` | 一致 |
| E6 | 全量门禁 | `bash tools/check.sh`（null）、`NEXCORE_JS=quickjs bash tools/check.sh`、`bash tools/check-host.sh` | 0 / 0 / 0 | `ALL CHECKS PASSED (NEXCORE_JS=null)`、`ALL CHECKS PASSED (NEXCORE_JS=quickjs)`、`HOST OK`；null 档 SKIP=3 | 一致 |

**耗时（仓库根，本机 macOS）**

| 命令 | 墙钟 | 备注 |
| --- | --- | --- |
| `bash experiments/build.sh`（引擎依赖重建） | 14.7s | 仅首次需要；已存在 `libquickjs.a` 时跳过下载与编译 |
| `bash tools/check.sh`（null） | 11.4s | SKIP=3（⑧⑨/smoke），⑮⑯⑰ 记 N/A |
| `NEXCORE_JS=quickjs bash tools/check.sh` | 18.2s | 全项真跑；Kotlin compile 仍为 `UNVERIFIED`（本机无 kotlinc） |
| `bash tools/check-host.sh` | 2.8s | 含 1f 的 1.5s 存活探针与 1g 的 200ms 预算用例 |
| `tests/interrupt.c`（编译 + 运行） | 0.6s + 0.5s | E1/E2 两条断言各自 `alarm(5)` 兜底未触发 |

**结论**：仓库根补跑结论与 §9 副本实跑**逐条一致**，无回退、无环境缺依赖导致的失败；未改判据、未改 golden、未改版本号、未改 `tools/check.sh`。本次仅新增文档与原始输出，按 6.7 V-4 不 bump 版本。仍不可执行项不变：收口 tag / push 未做（授权外），Kotlin 编译 `UNVERIFIED`（本机无工具链）。
