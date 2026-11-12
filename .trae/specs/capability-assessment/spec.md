# ct-NexCore 实际可用能力评估与补齐规格

- 状态：**已批准并实施**（S1–S5 完成；S6/S7 留待下一轮）
- 日期：2026-11-07
- 评估对象：`/Users/samxx/vscspace/ct-NexCore` 工作树（`src/` 骨架 + `build/` 实跑产物）
- 约束：评估期间未修改任何源码、未放宽任何检查；容器验证不可行，已如实记录

---

## 1. 结论

**当前状态不能承接真实业务。**

它目前是一个「ABI 形状正确 + 单向入队」的骨架：外部可以 `create` / `set_sink` / `send` / `stat` / `shutdown` / `free`，消息能进队列并被计数，但**没有任何路径把消息取出、交给 JS、把结果交回宿主**。`sink` 永远不会被调用，`bootstrap_js` 永远不会被读取。

可以证明的能力（真实跑出来的）：ABI 头形状、构建链、跨单元拓扑、入队、队列深度统计、错误码契约。
不存在的关键能力：JS 执行、出队、outbox、drain 回调、异步关闭、线程安全。

---

## 2. 证据

所有输出均为本机实跑（macOS arm64），未做任何修改后再跑。

### E1 QuickJS 没有被链接进任何构建产物

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 源码是否引用 QuickJS | `grep -rniE 'quickjs\|JS_NewRuntime\|JS_Eval' src/` | 仅 3 处**注释**：`src/NexCore.ct:13`、`src/protocol/protocol.h.ct:8`、`src/js_host/js_host.h.ct:6`。无代码、无 `#include <quickjs.h>` |
| 产物是否引用 | `grep -rniE 'quickjs\|JS_NewRuntime\|JS_Eval' build/` | rc=1，无匹配 |
| 生成 C 的 include | `grep -n '^#include' build/impl/*.c` | 只有 `"queue.h"` / `"js_host.h"` / `"NexCore.h"` / `"protocol.h"` + `<stdint.h>` `<stddef.h>` `<stdlib.h>` `<string.h>` |
| 最终二进制符号 | `nm build/bin/smoke \| grep -E ' _?JS_[A-Za-z]'` | rc=1，无匹配（注意 `_js_host_*` 是小写命名空间符号，不是 QuickJS） |
| 动态依赖 | `otool -L build/bin/smoke` | 仅 `/usr/lib/libSystem.B.dylib` |
| 未定义符号 | `nm -u build/impl/obj/*.o` | 只有 `_js_host_init`，无 `JS_*` |
| 链接行 | `check.sh` 实际打印 | `cc -std=c99 build/impl/NexCore.c build/impl/queue.c build/impl/js_host.c build/impl/protocol.c -o a.out` — 无 `-lquickjs`、无 `-lpthread` |

`#include` 到 `queue.h` / `js_host.h` 为止；QuickJS 头文件在全仓库 `src/` 中一次都没出现。

### E2 不存在任何执行 JS 字节码的路径

- `src/js_host/js_host.ct` 的 `init(q, bootstrap_len)` 只把字面量 `"bootstrap"`（9 字节）入队；`bootstrap_len` 仅用于 `< 0` 校验，与内容无关。
- 生成物 `build/impl/js_host.c` 同样是 `queue_push((q), ("bootstrap"), (9))`。
- `js_host::Host.turns()` 硬编码返回 `1`，没有 run loop。
- 全仓库唯一的真实 JS 执行入口在 `experiments/`：`experiments/out/bridge`（923,816 字节，`otool -L` 仍只依赖 libSystem，QuickJS 为静态链接）+ `experiments/third_party/quickjs/libquickjs.a`（3,917,056 字节，源码 39 个文件齐全）。`experiments/out/bridge.run.txt` 记录了真实执行（native 同步调用、Promise resolve/reject、异常、干净退出），但该二进制需要 `<script.js>` 参数，且**不属于 `src/` 交付物，`check.sh` 不构建它**。

结论：`src/` 侧 JS 执行能力为 0；`experiments/` 侧有可用的 QuickJS 验证原型，但没有接回 `src/`。

### E3 `send → queue → JS → outbox → drain → sink` 逐段状态

| 段 | 状态 | 证据 |
| --- | --- | --- |
| `nexcore_send` → 队列 | **通** | `queue::Ring.push`；smoke 实测 `depth=1` → send → `depth=2` |
| 队列 → JS | **空壳** | `src/` 内 `pop` / `dequeue` / `consume` 出现次数为 **0**；`queue.h.ct` 只导出 `open/push/depth/close`，注释写明「出队与线程安全留给后续阶段」 |
| JS → outbox | **不存在** | `build/impl/NexCore.c` 的 `struct NexCoreHandleImpl` 无 outbox 字段；全仓库无 outbox 实现，`NX_STAT_OUTBOX_DEPTH` 恒返回 0 |
| `nexcore_drain` | **空壳** | 生成 C：`(void)(max_wait_ms); if (out_count != 0) (*out_count) = 0; return NX_OK;` — 不取队列、不调 sink、不计时 |
| drain → sink | **空壳** | `impl->sink` 只在 `nexcore_set_sink` 中被赋值，全仓库无任何调用点 |

即：链路只有第一段（入队）是通的，中间三段不存在，最后一段（回调）从未发生。smoke 的 `drain rc=0 count=0` 恰好证明了这一点——队列里已有 2 条消息，drain 仍报投递 0 条。

### E4 关闭流程与跨线程队列都是占位

- `nexcore_shutdown` 只执行 `impl->state = NEXCORE_STATE_SHUT;`，没有 drain、没有 JS 关闭、没有等待、没有超时；`NX_E_CLOSE_TIMEOUT` 已定义但**没有任何代码返回它**。
- `nexcore_free` 要求先 `SHUT`，然后 `queue::Ring.close` + `free`。
- `src/` 内 `pthread|mutex|cond|atomic` 出现次数为 **0**。
- `queue.ct` 是 `malloc` 的 `char*` 数组，无锁、无条件变量、无阻塞语义；`push` 满时返回 `-2` 由上层映射为 `NX_E_BACKPRESSURE`。

结论：关闭是状态位开关，不是关闭序列；队列是单线程容器，不是跨线程队列。

### E5 本机构建与检查（可复现基线）

`./tools/check.sh` → `CHECK_RC=0`，14 项全绿，关键输出：

```
config: sizeof=88 v1=72 min=72 current=88 abi=1 protocol=1
create rc=0 handle=non-null
stat(after create) rc=0 depth=1
send rc=0
stat(after send) rc=0 depth=2
drain rc=0 count=0
stat(LAST_ERROR=18) rc=0 v=0
stat(reserved 10) rc=9
stat(next_free=19) rc=1
shutdown rc=0
send(after shutdown) rc=3
free rc=0
SMOKE OK
ALL CHECKS PASSED
```

统计键契约实测：`10..14` → rc=9（`NX_E_INTERNAL`）且 `*out` 保持哨兵值 `-424242` 未被写；`0/19/20/1024` → rc=1（`NX_E_INVALID_ARGUMENT`）；`1` → 1；`15/18` → 0。

这与已知基线一致：`sizeof=88/v1=72`、7 个导出符号（②c 7/7）、`SMOKE OK`、全绿；同时 `drain count=0` 复核了「drain 从不消费、从不调 sink」。

### E6 容器验证：不可行（如实记录，未做破坏性操作）

- `podman version` → Client `6.1.4`（brew，darwin/arm64，provider `applehv`），`RC=125`：`Cannot connect to Podman. ... dial tcp 127.0.0.1:65101: connect: connection refused`
- `podman machine list` → `podman-machine-default*`，applehv，created 13 months ago，last up 13 months ago，4 CPU / 4GiB / 100GiB
- `podman machine start` → `RC=125`：`Error: unable to connect to "gvproxy" socket at "/var/folders/.../podman-machine-default-gvproxy.sock"`
- 替代运行时：`docker` / `colima` / `lima` / `nerdctl` / `limactl` 全部 `absent`

**额外结构性障碍**：`ct` 编译器是 `Mach-O 64-bit executable arm64`（`/Users/samxx/.local/bin/ct`，`ct v0.19.1`）。即使 podman VM 可用，Linux 容器内也无法直接执行该二进制，`check.sh` 与 `tests/smoke` 都依赖 `ct build`。容器验证需要先有 Linux 版 `ct`，这是工具链前置条件，不是本次跑不通的原因。

因此本次结论全部基于本机构建；未做 `podman machine reset` 等破坏性尝试。

---

## 3. 缺口清单（按依赖顺序）

| 编号 | 缺口 | 现状 | 依赖 |
| --- | --- | --- | --- |
| G1 | 队列出队 `pop` | 不存在 | 无 |
| G2 | outbox（响应/事件出口） | 不存在 | G1 |
| G3 | QuickJS 运行时接入 `js_host` | 仅 experiments 原型 | G1、G2 |
| G4 | `bootstrap_js` 真正被读取并 `JS_Eval` | 字段从不解引用 | G3 |
| G5 | `nexcore_drain` 消费→执行→回调 `sink` | 恒返回 0 | G1、G2、G3 |
| G6 | 关闭序列（停收→排空→释放 JS→超时） | 只置状态位 | G5 |
| G7 | 跨线程队列（互斥/条件变量/JS 工作线程） | 0 处线程原语 | G6 |
| G8 | 统计真实采样（11 个恒 0 键 + 10..14） | 硬编码 0 / 报 `NX_E_INTERNAL` | G5、G6、G7 |

---

## 4. 最短依赖顺序补齐路径

**目标：让 `send → 队列 → JS → outbox → drain → sink` 端到端跑通一次，且不引入线程。**

1. **S1（G1）** `queue::Ring.pop`：`src/queue/queue.h.ct` 增 `pub int32_t pop(void *h, char **out_data, int32_t *out_len);`，`src/queue/queue.ct` 实现 FIFO 出队，所有权规则写死为「调用方负责 `free(*out_data)`」；`src/units.json` 的 `queue` methods 增 `pop`，重跑生成器。
2. **S2（G2）** 在 `NexCoreHandleImpl` 增加 outbox 持有（复用同一 `Queue` 实现，容量 `NEXCORE_DEFAULT_MAX_PENDING`），`nexcore_free` 一并释放。
3. **S3（G3+G4）** `js_host` 接入 QuickJS：`init` 持有 `JSRuntime *` / `JSContext *`，读取 `cfg->bootstrap_js`（首次真正解引用该字段）+ `bootstrap_len` 做 `JS_Eval`；新增 `pub int32_t eval(void *h, const char *src, int32_t len, char **out, int32_t *out_len)`。构建需在 `check.sh` 的 cc 行加 `-I experiments/third_party/quickjs` 与 `libquickjs.a -lm`。
4. **S4（G5）** `nexcore_drain` 变为真循环：pop inbox → `js_host::Host.eval` → push outbox → 逐条 `impl->sink(impl->sink_user, data, len)`，`*out_count` 写实际投递条数；`max_wait_ms` 用于空轮询上限（无消息立即返回）。
5. **S5（G6）** `nexcore_shutdown` 改为：置「停止接收」→ drain 剩余 → `js_host::Host.shutdown`（`JS_FreeContext/JS_FreeRuntime`）；超过预算返回 `NX_E_CLOSE_TIMEOUT`。
6. **S6（G7）** 引入 worker 线程 + 互斥/条件变量，把 `js_host` 移出调用线程；队列加锁。
7. **S7（G8）** 按实际事件给 11 个恒 0 键与 10..14 补采样。

S1→S2→S3→S4→S5 是端到端可用的最短闭环；S6 是「不阻塞调用方」的性能/架构要求，可在闭环之后再上；S7 依附于前面的真实事件。

---

## 5. 验收标准（全部可执行）

- A1：`nexcore_send("...")` 后 `nexcore_drain(h, 0, &n)` 返回 `NX_OK` 且 `n >= 1`，注册的 sink 被调用且收到的字节与 JS 输出一致。
- A2：`cfg.bootstrap_js = "globalThis.__ok = 1;"` 后 runtime 内可观察到副作用（通过一条 `JS_Eval("__ok")` 断言），证明 bootstrap 真被读取执行。
- A3：`nm build/bin/smoke | grep -E ' T _?JS_Eval$'` 命中，`otool -L` 不再只有 libSystem（静态链接则 A3 以 `nm` 命中为准）。
- A4：sink 未被注册时 `drain` 仍消费队列且不崩溃（投递条数与 `stat(OUTBOX_DEPTH)` 一致）。
- A5：`nexcore_shutdown` 在 JS 长时间执行时返回 `NX_E_CLOSE_TIMEOUT`（需 S6 后可测）；S5 阶段至少验证 shutdown 后 `send` 返回 `NX_E_SHUTDOWN`、`drain` 返回 `NX_E_SHUTDOWN`。
- A6：`tools/check.sh` 新增对应检查项（不复用旧编号），负例各失败一次后恢复，`SCRIPT_RC=0`。

---

## 6. 明确不做

- 不为了让检查变绿而放宽 ②b/②c/⑥ 的严格度。
- 不在 S1–S5 阶段引入线程（避免同时改语义与并发）。
- 不删除 `experiments/` 原型。
- 不为容器验证临时改 `check.sh` 或源码；容器前置条件（Linux 版 `ct`、可用的 podman VM）由环境侧解决。

---

## 7. 需要批准

请确认第 4 节的 **S1–S5 作为本轮实施范围**（S6/S7 留到下一轮），以及第 5 节验收标准是否接受。批准后按 `tasks.md` 逐项实施，每项完成后跑一次 `tools/check.sh`。
