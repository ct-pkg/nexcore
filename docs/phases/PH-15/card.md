# PH-15 卡：K-6 异步 `Shell.spawn` 的前置实现

> 状态：**已冻结（2026-11-16）**，待开工。`version-minor`：**15**。
> 上游：[PH-13 遗留 ABI 面评审](../../reviews/nexcore-ph13-k6-k7-abi-surface-review.md) §0/§2/§5.2（K6-A 零 ABI）、[PH-13 卡](../PH-13/card.md) §10 K-6、[async-threading.md](../../async-threading.md) §2、[acceptance.md](../../acceptance.md) ACC-A-08 / ACC-C-02 / ACC-C-08。
> 禁止改动：`docs/roadmap.md`、`abi/*`、`src/NexCore.h.ct`、`.ai-engineering`、7 个导出符号、`NEXCORE_CONFIG_CURRENT_SIZE`。

---

## 1. 归属与顺序

| 顺序 | 交付物 | 归属 | 依据 |
| --- | --- | --- | --- |
| 1 | inbox 线程安全（互斥量 + 条件变量 + 锁外唤醒） | 库层（`src/queue/`、`src/NexCore.ct` 的 `send`） | 评审 F18–F19：现实现无锁，多线程 send 是 UB |
| 2 | `nexcore_drain` 尊重 `max_wait_ms`、可被 send/shutdown 唤醒、单 drain 准入 | 库层（`src/NexCore.ct`） | 评审 F20；ACC-A-08/ACC-C-08 已写判据 |
| 3 | 宿主异步 `Shell.spawn`（worker + 分片回投 + bootstrap Promise + 回收） | 宿主层（`hosts/shell/main.c` 的 bootstrap 与 Provider） | K6-A：零 ABI，宿主持有子进程 |

顺序固定为 T1 → T2 → T3：T3 的 worker 从非 drain 线程 `nexcore_send`，依赖 T1；drain 循环等子进程输出，依赖 T2。**T3 不得先于 T1/T2 合入。**

---

## 2. 不变量

| # | 不变量 | 核对 |
| --- | --- | --- |
| I-1 | 7 个导出函数、`abi/abi-layout.txt`、`abi/expected-forward-decls.txt`、`CURRENT_SIZE=144` 全部不动 | E7 |
| I-2 | 不新增统计键；`NX_STAT_NEXT_FREE` 仍为 19 | G-⑥ |
| I-3 | 两档 rc=0；null 档 SKIP=3；⑮⑯⑰ 不回退 | E8 |
| I-4 | `sink` 只在 drain 线程调用；worker 只 `nexcore_send`，不直接调 `sink` | 代码审查 + E5 |
| I-5 | `free` 之前先 `shutdown`；`shutdown` 之前 join 所有 worker；`free` 之后不得再 `send` | 代码审查 + E5 |
| I-6 | 已接受消息保持 FIFO（G-⑦） | E8 |
| I-7 | 不新增协议方法/事件/错误码；不升协议版本；回投走既有 `nexcore_send` | E7、E8 |
| I-8 | 不在锁内调用 `sink`、JS 或 `nexcore_*`；不自旋 | 代码审查 |

---

## 3. 设计定死项

### 3.1 D1 inbox：锁 + 条件变量，锁外唤醒

- `struct Ring` 追加 `pthread_mutex_t lock`、`pthread_cond_t cond`、`int32_t closed`；`open` 初始化，`close` 销毁。
- `push`/`pop`/`depth`/`close` 全程持锁；`push` 的容量检查、`malloc` 与拷贝都在锁内完成，**解锁后** `cond_signal`。
- 新增 `wait`/`wake_all`（`pub`，同步 `src/units.json`，并由 `python3 tools/nexcore_gen.py --spec src/units.json --src src` 重新生成 `queue_map.m.ct`；不改 GENERATED 文件本身；G-⑤b 与 `check.sh` 第 0 项漂移检查必须 PASS）：
  - `wait(h, timeout_ms)`：锁内检查 `count > 0 || closed`，不成立才等待；返回 `0` = 有数据或被唤醒（允许虚假唤醒），`-4` = 超时，`-5` = 已关闭。
  - `wake_all(h)`：只 `cond_broadcast`，不释放数据；由 `shutdown` 调用。
- `nexcore_send` 在 `queue::Ring.push` 成功后调用 `wake_all`（锁外）；失败路径不唤醒。

### 3.2 D2 `nexcore_drain` 的等待与准入

- `max_wait_ms < 0`：无限等待，直到 inbox 有数据或句柄进入 shutdown；`0`：非阻塞；`>0`：最多等待该毫秒数。
- 等待循环在每次唤醒后重新检查：`state != LIVE` → 返回 `NX_E_SHUTDOWN`、`*out_count=0`；`depth > 0` → 处理并投递；否则继续等待或按超时返回。
- `nexcore_shutdown` 置 `STOPPING` 后调用 `queue::Ring.wake_all`，阻塞中的 drain 必须在 **≤100 ms** 内返回 `NX_E_SHUTDOWN`（ACC-C-08）。
- 单 drain 准入：句柄内维护 `drain_active`（生命周期锁保护）；第二个并发 `nexcore_drain` 立即返回 `NX_E_THREAD`；**所有返回路径**都要清标志，不得把标志留在错误分支（ACC-A-08）。
- `*out_count` 仍写实际投递条数；无 sink 时投递数为 0。

### 3.3 D3 宿主异步 `Shell.spawn`：零 ABI

- Provider 名字改为 `Shell.spawn.start`，加入 `NAMES`；`on_native` 登记任务、启动 worker 线程并**立即**返回 `{"id":N}`。
- `build_bootstrap` 追加 Promise 包装：`Shell.spawn(cmd)` 生成 id、登记 `pending[id]`、调用 `__nx_native("Shell.spawn.start",[id,cmd])`、**立即返回 Promise**。
- 宿主 worker（每个 spawn 一条线程）读子进程 stdout，每片拼成 JS 片段 `__nxSpawnChunk(id,"<JSON 转义>")` 调 `nexcore_send`；退出时投 `__nxSpawnExit(id,code)`；Promise 在 drain 线程收到 exit 片段后 resolve `{code,stdout}`。
- 回投失败（`nexcore_send` 非 `NX_OK`）时 worker 不再读、把 `code` 置 `-1`，保留已收 stdout；**不得伪装成功**。
- `native_user` 指向宿主上下文（句柄 + spawn 表 + pending 计数，互斥量保护）；`h` 在 `create` 成功后写入上下文。
- 宿主主流程：eval 返回后，只要 pending > 0 就 `nexcore_drain(-1)`；归零后 join 所有 worker，再 `stat`/`shutdown`/`free`。
- 子进程所有权与回收留在宿主；kill/取消、PTY/stdin 不在本卡（§3.5）。库侧不出现任何进程概念。

### 3.4 D4 JS 形状与测试脚本

- `Shell.spawn(cmd)` 统一返回 Promise；不再保留同步返回 `{code,stdout}` 的同名形态（PH-13 E4 的同步形状由本卡取代，见 §9 裁定 3）。
- `check-host.sh` 的 1h（同步 spawn 两条正例）在同一任务内改写为 `.then` 形式，期望输出仍是 `hi` 与 `3`。
- 异步用例脚本必须以 `null` 收尾：Promise 被 `JSON.stringify` 会得到 `{}`，会污染 stdout 并让“输出顺序”断言失真。

### 3.5 D5 取消不在本卡

不实现 `Shell.spawn.cancel`/kill 交互、PTY、stdin、进程组；登记 K-6a（见 §6）。理由见 §9 裁定 4。

---

## 4. 任务清单

| task-id | deliverable | verify | commit | rollback | effort | 归属 | no-test |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PH15-T1 | `src/queue/queue.ct`/`queue.h.ct` 加锁与条件变量、`wait`/`wake_all`；`src/NexCore.ct` 的 `send` 锁外唤醒；`src/units.json` 同步并重新生成 `queue_map.m.ct` | `tests/queue_mpsc.c`：8 线程 × 10⁴ push、单线程并发 pop，`accepted + rejected == 80000`，已接受项恰好各出队一次；普通档 rc=0；`-fsanitize=thread` 档输出无 `WARNING: ThreadSanitizer` 且 rc=0 | `test(queue): PH15-T1 红` + `feat(queue): PH15-T1 inbox 加锁` | 成对 `git revert`；T2/T3 未合入时无依赖 | M | 库层 | — |
| PH15-T2 | `nexcore_drain` 支持 `-1/0/>0`、send/shutdown 唤醒、单 drain 准入（`NX_E_THREAD`） | `tests/drain_block.c`：空队列 `drain(-1)` 在 200 ms 时仍阻塞；另一线程 send 后 ≤100 ms 返回且 `*out_count==1`；shutdown 后 ≤100 ms 返回 `NX_E_SHUTDOWN`；双线程 barrier 并发 drain，恰好一个阻塞收到、另一个 `NX_E_THREAD`，重复 100 次全过 | `test(drain): PH15-T2 红` + `feat(drain): PH15-T2 阻塞 drain 与单线程准入` | 成对 `git revert`；T3 未合入时无依赖 | M | 库层 | — |
| PH15-T3 | 宿主异步 spawn：`Shell.spawn.start` Provider、worker + 分片回投、bootstrap Promise、drain 循环与 join；`check-host.sh` 1h 改写 + 新增异步用例 | `bash tools/check-host.sh`：异步用例 stdout 中 `tick` 行在 `hi` 行之前，且总耗时 <2 s；1h 改写后仍输出 `hi` 与 `3`；末行 `HOST OK` | `test(host): PH15-T3 红` + `feat(host): PH15-T3 异步 spawn` | 成对 `git revert`；撤回后回到 PH-13 同步 spawn | M | 宿主 | — |

**任务清单每项可独立验收**：每行都有自己的 `verify`、`commit`、`rollback` 与 `effort`；T1/T2 可各自回滚，T3 的回滚只撤回宿主异步化并回到 PH-13 同步 spawn。

**红证要求（T1–T3）**：每个 `test(...) 红` 必须是同 task 实现提交的父提交，红证写 `baseline: <sha>` + 命令 + 原始输出 + 退出码。
- T1 红：现行无锁队列在 TSAN 下报 `data race`（`src/queue/queue.ct:38-53` / `:55-66`）。
- T2 红：现行 `nexcore_drain` 立即返回、`*out_count==0`（`src/NexCore.ct:354-361`）。
- T3 红：现行 `Shell.spawn` 返回普通对象，`.then` 为 `undefined` → `TypeError`（PH-13 §9 E4 可作对照）。

---

## 5. 退出判据

| # | 判据 | 命令与阈值 | 对应 |
| --- | --- | --- | --- |
| E1 | inbox MPSC 无竞争、无丢失/重复 | `cc -std=c99 -Wall -Wextra -Werror -I build/impl tests/queue_mpsc.c build/impl/queue.c -lpthread -o build/bin/queue_mpsc && ./build/bin/queue_mpsc`：rc=0，`accepted+rejected=80000`，已接受项各出队一次 | T1 |
| E2 | TSAN 零报告 | 同一测试追加 `-fsanitize=thread` 重编重跑：stderr 不含 `WARNING: ThreadSanitizer`，rc=0 | T1 |
| E3 | `drain(-1)` 真阻塞、`send` 真唤醒 | `tests/drain_block.c`：空队列起 200 ms 时仍在阻塞；send 后 ≤100 ms 返回且 `*out_count==1` | T2 |
| E4 | shutdown 唤醒与单 drain 准入 | 同夹具：shutdown 后 ≤100 ms 返回 `NX_E_SHUTDOWN`；双线程 barrier × 100 次，每次恰好一个阻塞收到、另一个 `NX_E_THREAD` | T2 |
| E5 | 异步 spawn 不被求值阻塞 | `bash tools/check-host.sh`：`Shell.spawn("sleep 0.2; echo hi").then(r=>Shell.print(r.stdout.trim()));Shell.print("tick");null` 的输出中 `tick` 行号 < `hi` 行号，总耗时 <2 s | T3 |
| E6 | 既有同步形状完成迁移 | `check-host.sh` 1h 改写后仍输出 `hi` / `3`；脚本末行 `HOST OK` | T3 |
| E7 | ABI 面未破 | G-⑩ 仍 7 个 `nexcore_*`；G-⑪ golden 零差异；G-⑫ 仍 1 行；`CURRENT_SIZE=144`；`grep -c spawn abi/abi-layout.txt` == 0 | 全部 |
| E8 | 全量门禁 | `bash tools/check.sh`（null）与 `NEXCORE_JS=quickjs bash tools/check.sh` 均 `ALL CHECKS PASSED`，SKIP=3；G-⑦ FIFO 仍 PASS；`bash tools/check-host.sh` 输出 `HOST OK` | 全部 |

**编译口径**：`tests/drain_block.c` 的编译/链接参数与 `tools/check.sh` ⑨ 相同（`-I build/abi -I build/impl -I src/js_backend -I src/queue -I src/protocol`，链接 `build/impl/*.c` 与 `experiments/third_party/quickjs/libquickjs.a -lm -lpthread`），且要求 quickjs 档。

**UNVERIFIED 规则**：Windows/Android 的 `fork/exec` 与信号语义不在本机可验范围，登记环境缺口，不记通过；本机无 TSAN 时 E2 记 UNVERIFIED，不得用不跑代替零报告（当前本机 `cc -fsanitize=thread` 探针可用）。

---

## 6. 遗留项（预期）

| # | 项 | 处置 |
| --- | --- | --- |
| K-6 | 异步 `Shell.spawn`（MVP） | 本 phase 收口后关闭 |
| K-6a | 取消/kill、PTY、stdin、进程组 | 挂账；无验收判据，另开 |
| K-6b | 回投载体是 JS 片段而非协议 JSON 信封 | 挂账（评审 Q3/U4）；过渡期在收口说明标注“非契约” |
| K-6c | Windows/Android 进程语义 | 环境缺口，UNVERIFIED |
| K-6d | 统计键 20 / 中断原因 | 不做（PH-14 已裁定） |

---

## 7. 非目标

- 不新增 ABI 符号/结构/回调，不升 `NEXCORE_ABI_VERSION`，不改 `abi/*`。
- 不做 K6-C（库拥有子进程）、不做 ops 表、不做 PTY/stdin/进程组/取消。
- 不做协议 JSON 信封分发；不改协议文档与验收文档。
- 不改 `tools/check.sh` 的既有编号与判据；TSAN 以独立命令验收（见 §9 裁定 1）。
- 不做 Windows/Android 适配。

---

## 8. 收口动作

| 项 | 动作 | 判据 |
| --- | --- | --- |
| 版本 | `docs/README.md` 状态行 `v0.14.0` → `v0.15.0`（若 PH-14 已收口；否则按实际上一版 +MINOR=15） | 与收口提交同提交；`g4-version` 四项核对通过 |
| 收口 tag / push | 按当前项目授权**不执行**（与 PH-13 同口径） | 补跑命令：`git tag -a v0.15.0 -m "PH-15 <sha>" <sha> && git push origin v0.15.0` |
| 证据 | 收口时在卡内补 E1–E8 原始输出与红证表 | 每条带命令、rc、原文摘要 |

---

## 9. 变更记录（battle 裁定，2026-11-16）

| # | 冲突/待裁点 | 裁定 | 理由 |
| --- | --- | --- | --- |
| 1 | T1 的 TSAN 是否新增 `check.sh` 编号 | **不新增**；普通档与 TSAN 档都以卡内单条命令验收 | 不改既有门禁编号与判据（PH-13 §7）；TSAN 需要额外编译变体，强行并入会让 `check.sh` 变慢且改变门禁语义 |
| 2 | 评审 T2 只写“阻塞 + send 唤醒”，但 ACC-A-08/ACC-C-08 还要求单 drain 准入与 shutdown 唤醒 | **扩入 T2**：`NX_E_THREAD` 准入 + shutdown ≤100 ms 唤醒 | 阻塞 drain 没有 shutdown 唤醒会挂死；多线程 drain 没有准入是 UB。两项都是既有验收条目，不能留给后续 |
| 3 | 异步化后 `Shell.spawn` 的旧同步形状是否保留 | **`Shell.spawn` 统一返回 Promise，不新增 `spawnSync`**；`check-host.sh` 1h 在同任务改写；PH-13 E4 的同步形状被取代 | 两套同名语义 = 两套真相；microtask 泵已落地（评审 F25），Promise 是唯一形状。宿主是本仓唯一消费者，迁移面可控 |
| 4 | 评审 §2.5 把取消列进 MVP，但本卡 T3 的验收只有异步顺序 | **取消移出本卡**，登记 K-6a | 本仓无取消验收判据，也拿不出独立可红证据；把未验证的交付物塞进 T3 违反 6.6 T-3 |
| 5 | T1 测试用队列单元还是 `nexcore_send` 全链路 | **`tests/queue_mpsc.c` 以队列单元 8×10⁴ 压测取证**；`nexcore_send` 的端到端并发由 T3 的 worker 回投 + G-⑨ 覆盖 | 先锁定竞争源；TSAN 在 QuickJS 全链路上噪声大，会把引擎内部与队列竞争混在一起，红证不可判 |
| 6 | 异步回投的排队载体 | **过渡期用 JS 片段**（评审 Q3 选项 A），在收口说明标注“非契约” | 协议 JSON 信封分发属 P2 后续（K-6b）；先兑现异步语义，不把协议改造塞进本 phase |
| 7 | PH-14 与 PH-15 的并行关系 | **可并行开卡，但 PH-15 的 T3 必须等 T1/T2 落地**；建议 PH-14 先开工 | 两张卡无代码依赖；PH-15 内部有严格顺序，T1/T2 是 T3 的运行时前提 |
| 8 | T1 夹具在无锁基线下先 SIGSEGV 或挂死，红证怎么取得 | **红证以 TSAN 档为准**（rc=133 + 8 处 `WARNING: ThreadSanitizer: data race`）；普通档基线 rc=139 一并记录；夹具加 30 s 看门狗（触发时 rc=12），避免挂死吞掉判据 | 无锁实现的 `count` 竞争会让 pop 的移位规模与容量脱钩，普通档红证无法稳定跑到计数断言；TSAN 在第一次冲突访问即报告，正是卡面 §4 指定的红证来源。看门狗只在实现违约时触发，转绿后不参与判定 |
| 9 | D2 写「shutdown 置 STOPPING 后调用 `wake_all`」，但广播不改变队列谓词 | **新增 `queue::Ring.close_input`**：置 `closed` 后持锁广播、不清数据；shutdown 改调它。`wait`/`wake_all` 的 T1 语义不变 | 实测 `wake_all` 会丢唤醒：广播若落在 drain「检查 state → 进入 cond_wait」之间，等待者随后仍看到 `count==0 && !closed` 并睡死。`closed` 是粘性谓词，进入等待前后都不会漏；`pop` 仍可取走已入队数据，释放仍归 `close`。属 T2 交付面内的最小修正，不改 ABI |
