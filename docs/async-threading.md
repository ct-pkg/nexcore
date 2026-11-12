# 异步、线程与生命周期

本文描述正常的调用路径，以及更容易出错的失败路径：超时、取消、关闭、晚到响应、重入、背压、JS 失控。所有伪代码是契约级示意，不是实现。

## 0. 已实测的参考路径与本文的差异

已实测跑通的路径是：

- JS→native→JS 同步调用可用；
- native 返回 Promise，worker 只写 mutex 队列；
- 主线程 drain 后 resolve Promise，再泵 `JS_ExecutePendingJob`，`await` 后续代码正常执行；
- JS→C 反向调用（`JS_GetPropertyStr` + `JS_Call`）可用；
- JS 异常、native 抛错、Promise reject 三路错误都能传到对端。

本文沿用其中的**队列 + drain + resolve + pump**机制，但把“运行 JS 的线程”从主线程改为**专用 JS 线程**，以便 UI 不被 JS 阻塞。这个差异需要在 P2 复测：

- 在专用线程创建 runtime 后调用 `JS_UpdateStackTop`；
- 线程栈大小（建议 8 MiB）与 `JS_SetMaxStackSize` 的配合；
- 平台主线程约束（某些平台 API 只能在主线程调用）由平台 transport 负责，不进入 JS 线程。

CT 不提供线程原语与闭包：线程/锁/条件变量由 shim 提供，回调上下文只能是 `void *userdata`；JS 侧上下文用 `JS_SetContextOpaque`。本文的线程归属、断言与关闭顺序全部由桥接层自己实现，不假设语言提供。

## 1. 线程模型

每个 `NexCoreHandle` 恰好包含：

| 线程 | 谁创建 | 职责 | 禁止 |
|---|---|---|---|
| JS 线程 | Bridge（`nexcore_create` 内部） | 拥有 `JSRuntime/JSContext`；解析入站 JSON；执行 JS；泵微任务；处理超时；生成出站消息 | 调用平台 API；阻塞等待 native；调用 sink |
| drain 线程 | 平台 transport（Swift/Kotlin） | 循环 `nexcore_drain`；把出站消息解码并调度到 UI 线程 | 调用 `JS_*`；在 sink 内 `shutdown/free` |
| 调用线程（任意） | 平台 | `nexcore_send` 投递请求/响应/通知 | 直接调用 JS；假设同步完成 |
| UI 主线程 | 平台 | 渲染、平台能力、执行生成的 UI handler | 阻塞等待 JS；直接持有 `JSValue` |

规则：

- **JS 是单线程的**：一个 runtime 只属于一个 OS 线程；所有 `JS_*` 调用都发生在 JS 线程（`nexcore_create` 的初始化也在 JS 线程完成，调用方只等待结果）。
- **sink 只在一个线程**：drain 线程。平台绑定负责把消息切到 UI 线程；Bridge 不假设 UI 线程存在。
- **没有平台回调进入 Bridge**：不使用 `DispatchQueue`、`Handler`、`JavaVM` 作为唤醒机制。唤醒只靠互斥量 + 条件变量，平台侧用一个阻塞在 `nexcore_drain(-1)` 的专用线程承接。
- **不阻塞等待**：JS 线程不等 native 的返回值；drain 线程不等 JS 完成某个请求。唯一的 join 发生在 `nexcore_shutdown` 或 `nexcore_free`（见 §7.1）；调用线程都必须是非 JS 线程。
- **宿主只提供两个可选回调**：`now_ms` 与 `log`（签名、调用线程、NULL 语义见 design.md §1.3）。线程 create/join/destroy 由内部 shim 提供，drain 唤醒用 mutex+condvar，**没有宿主侧的唤醒写端**；宿主唯一需要自己创建的是那个阻塞在 `nexcore_drain` 的专用 drain 线程。

## 2. 队列与唤醒

```
调用线程 ──nexcore_send──> [inbox: MPSC 字符串队列] ──> JS 线程
                                                      │
                                                      ├─ 解析、执行、泵微任务
                                                      │
JS 线程 ────────────────> [outbox: 响应队列 + 事件队列] ──> drain 线程 ──sink──> 平台
```

| 队列 | 生产者 | 消费者 | 上限 | 溢出策略 |
|---|---|---|---|---|
| inbox | 任意线程 | JS 线程 | 4096 条 / 8 MiB | `nexcore_send` 返回 `E_BACKPRESSURE` |
| outbox-response | JS 线程 | drain 线程 | `max_pending` × 2 | 视为内部故障，`E_INTERNAL` 并关闭 |
| outbox-event | JS 线程 | drain 线程 | 4096 条 / 8 MiB | 丢事件 + `$error(E_BACKPRESSURE)` |
| pending UI→JS | JS 线程 | JS 线程 | 1024 | 新请求 `E_BACKPRESSURE` |
| pending JS→UI | JS 线程 | JS 线程 | 1024 | 新请求 `E_BACKPRESSURE` |

- inbox 是 MPSC；`nexcore_send` 在锁内做容量检查并复制字符串，锁外 `cond_signal`。
- outbox 实际是单生产者（JS 线程）双队列；drain 先取 response，再取 event，保证响应不被事件饿死。
- 所有条件变量等待都带超时（最近 pending 截止时间）；`nexcore_drain(-1)` 是唯一的无限等待，`shutdown` 会唤醒它。
- 不做 busy-wait；不做自旋锁；不在锁内调用 sink、JS 或 Core。

## 3. 一次 turn 的精确边界

JS 线程每处理一条入站消息，执行一个 turn：

```
turn(msg):
  1. 解析信封（JS_ParseJSON），校验 id/method/params
  2. 查 pending 重复、方法白名单、方向
  3. 登记 pending（id, direction, deadline, method）
  4. 调用 JS dispatcher（UI→JS）或 __nx_on_response（JS→UI 的响应）
  5. 等 JS 调用栈展开
  6. pump_jobs(deadline)           # 见 §5
  7. 处理到期 pending              # 见 §6
  8. 投递本 turn 产生的 core 事件（若有）
  9. 回到等待
```

关键点：

- **第 4 步不等待 Promise**。UI→JS 的 dispatcher 返回 Promise 后由 JS 的 `.then` 在微任务里调用 `__nx_reply`；Bridge 不持有 `JSValue`。
- **第 6 步之前 JS 调用栈必须已经展开**。不能在 JS 执行期间泵微任务；否则会重入。
- **第 7 步在每个 turn 后都执行**，而不是只靠定时器线程；没有独立定时器线程。
- **core 事件在第 8 步投递**，避免 Core 在 JS 调用栈内回调 JS 造成重入。

## 4. Promise 与 pending map

### 4.1 JS→UI 请求（JS 发起）

```js
// JS bootstrap
function call(method, params, timeoutMs) {
  const id = nextId();
  const p = new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
  });
  try {
    __nx_call(id, method, params, timeoutMs); // C 函数：登记 + 入 outbox
  } catch (e) {
    pending.delete(id);
    throw e;
  }
  return p;
}
```

- **JS 侧 pending**：`id -> {resolve, reject}`；在调用 C 之前登记，保证同步返回的响应也能匹配（实际响应一定异步，但顺序上安全）。
- **C 侧 pending**：只存 `{id, direction, deadline, method}`，不存 `JSValue`。
- 响应到达：JS 线程解析 → 查 C pending → 删除 → 调用 `__nx_on_response(id, ok, value|error)` → JS 删除 pending → resolve/reject。
- 超时：C 删除 pending → 构造错误响应 → 调 `__nx_on_response(id, false, error)` → JS 删除并 reject。
- 取消：对端收到 `$cancel` 后删除 pending 并回 `E_CANCELLED`；本端收到响应后删除。
- 关闭：JS 线程遍历 JS pending，逐个 `reject(E_SHUTDOWN)` 并清空；C pending 直接清空。

**Exactly-once 保证**：删除 pending 是 settle 的唯一入口；删除失败（已被超时/取消）就丢弃响应并计数。Promise 本身只会 settle 一次，但必须在删除后再调用 resolve/reject，避免 handler 重入导致重复。

### 4.2 UI→JS 请求（native 发起）

```
native: nexcore_send({"id":7,"method":"app.refresh","params":{}})
JS 线程: parse -> 校验 -> C pending 登记 -> __nx_dispatch(7,"app.refresh",params)
JS dispatcher: 调用注册的 handler；成功/失败后调用 __nx_reply(7, ok, value|error)
C: 查 pending(7) -> 删除 -> 序列化 result/error -> outbox(response)
drain: sink({"id":7,"result":...})
```

- `__nx_dispatch` 由生成的 JS 代码实现：一个显式 `switch (method)` 或对象表，不做反射。
- JS 必须保证 `__nx_reply` 恰好调用一次；bootstrap 用 `replied` Set 防御重复。
- 如果 JS dispatcher 同步抛异常，C 捕获并直接生成 `E_JS_EXCEPTION` 响应；同时删除 pending。
- 如果 dispatcher 返回的 Promise 永远不 settle，C 的超时会兜底（§6）。
- C 不保存 Promise 或 `JSValue`；因此 JS GC 不会影响 pending 表。

### 4.3 Core 调用（JS→Core）

- `__nx_core_call(method, args)` 是 C 函数，在 JS 线程同步执行；不登记 pending、不入队、不产生 id。
- 生成包装函数把 `JSValue` 参数解码为 C 类型，调用 Core，编码返回值。
- Core 不得阻塞；不得调用 `nexcore_send`；不得回调 JS。
- 错误通过 `int32_t` 返回，包装函数转成带 `code` 的 JS 异常；生成的 JS `async` 函数把它变成 rejected Promise。

## 5. 微任务泵送

### 5.1 时机

只在以下位置泵送，且只在 JS 调用栈展开后：

1. 一个 UI→JS 消息的 dispatcher 返回之后；
2. 一个 JS→UI 响应交付给 JS 之后；
3. 一批超时处理之后；
4. `$shutdown` 通知处理之后；
5. bootstrap / app 脚本 `JS_Eval` 返回之后。

不在以下位置泵送：

- `nexcore_send` 内（可能在任意线程，且不解析）；
- sink 内（drain 线程，不能碰 JS）；
- JS handler 执行期间（会重入）；
- Core 函数内（同线程但会重入）。

### 5.2 泵到空还是泵一轮

**泵到空，但有 turn 预算。** 理由：

- Promise 链的语义要求：一个请求的 `.then` 链应在下一条 native 消息之前完成，否则事件顺序会变得不可预测。
- “泵一轮”会把一个长链拆到多个 turn，导致同一请求的后续逻辑与其他请求交错，难以审查。
- 无上限泵空会被 `Promise.resolve().then(loop)` 拖死，因此必须有预算与中断。

```
pump_jobs(episode_start, yield_quota, closing):
  jobs = 0
  while JS_IsJobPending(rt):
      now = now_ms()
      if !closing:
          if now >= episode_start + pump_budget_ms:   // 失控预算（D-05），默认 200 ms
              set_abort()                        // 只有非 closing 才置 abort_requested
              return E_JS_RUNAWAY
      else:
          if now >= episode_start + shutdown_hard_limit_ms:
              set_close_abort()                  // 关闭硬中断；不置 abort_requested
              return E_CLOSE_ABORT
          // grace 期（< shutdown_budget_ms）什么都不做：
          // 这里的检查只在两个 job 之间执行，单个不返回的 job 由 interrupt handler 在硬上限抢占
      if jobs >= yield_quota:
          return YIELD                           // continuation，不重置 episode_start
      r = JS_ExecutePendingJob(rt, &ctx)
      if r < 0:
          if closing && close_abort: return E_CLOSE_ABORT
          if abort_requested: return E_JS_RUNAWAY
          report_js_exception()
          return E_JS_EXCEPTION
      if r == 0: break
      jobs++
  return OK
```

**关键约束（P1-a）**：`set_abort()` **只能**出现在 `!closing` 的失控分支；`closing` 分支只能 `set_close_abort()`。若在关闭 turn 里照抄失控分支，500 ms 就会把正常关闭打成 FATAL——这是 P0 的反向翻车。`E_JS_RUNAWAY` 与 `E_CLOSE_ABORT` 是 pump 的内部返回值，对外分别映射为协议错误与 `NX_E_CLOSE_TIMEOUT`。

**四个时间常量（P1-d：三个 200 不是同一个东西，必须分开命名）**：

| 常量 | 值 | 作用域 | 含义 |
|---|---|---|---|
| `pump_budget_ms` | 200 | 实现 | 非关闭 turn 的连续泵送窗口；超窗 → `abort_requested` → `E_JS_RUNAWAY` |
| `shutdown_budget_ms` | 500 | 实现 | 关闭 turn 的优雅预算（grace）；期内 interrupt 返回 0 |
| `shutdown_hard_increment_ms` | 200 | 实现 | 硬上限相对 grace 的**增量**；`shutdown_hard_limit_ms = shutdown_budget_ms + shutdown_hard_increment_ms = 700` |
| `shutdown_schedule_slack_ms` | 200 | **仅验收** | 断言 `shutdown` 在 700 ms 后多久内返回的调度余量；不进入实现、不是 ABI 常量 |

> 验收文本里必须写全"硬上限 700 ms（500 grace + 200 增量）+ 200 ms 调度余量"；不得把两个 200 混写成一个"700+200"。

**预算语义（D-05）**：`MAX_JOBS_PER_TURN = 10000` 是**让出配额**，不是失败阈值。

- 达到配额时返回 `YIELD`：当前 turn 结束，立即开始**continuation turn** 继续泵同一个 job 队列；在 job 队列清空前**不处理 inbox 消息**。因此同一 promise 链仍先于下一条消息完成，10,000 链不会被拆散或丢弃。
- continuation turn 只重置 `jobs` 计数，**不重置 `hard_deadline`**；`hard_deadline` 从本次泵送 episode 的第一个 turn 起算，因此无限链仍会在 200 ms 内被中断。
- 三个标志互不复用：
  - `stop_requested`：优雅关闭，只让 JS 线程停止取新消息，**不作为 interrupt 条件**；
  - `abort_requested`：失控中止，由单脚本 50 ms 或连续泵送 200 ms 置位，产生 `E_JS_RUNAWAY`/FATAL；
  - `close_abort`：关闭硬中断，只在关闭 turn 超过硬上限时置位，产生 `NX_E_CLOSE_TIMEOUT`，**不产生 `E_JS_RUNAWAY`**。
- `JS_SetInterruptHandler` 在 create 时安装；它在 QuickJS 执行 JS 字节码期间被周期性回调，因此**能抢占单个不返回的 job**（`while(true){}`），这是与 `pump_jobs` 顶部 deadline 互补的另一半：
  - 正常运行：单脚本 > 50 ms 或连续泵送 > `hard_deadline` → 置 `abort_requested`，返回非 0；
  - 关闭 turn（`closing==true`）：grace 期（`shutdown_budget_ms`，默认 500 ms）内**返回 0**，让 `.catch/.finally` 有机会执行；超过**硬上限** `shutdown_hard_limit_ms = shutdown_budget_ms + shutdown_hard_increment_ms`（500 + 200 = 700 ms）后置 `close_abort` 并返回非 0，终止当前 job。
- **单 job 抢占保证**：`pump_jobs` 的 deadline 检查只在两个 job 之间执行；若单个 job（如某个 `.catch` 内的同步死循环）永不返回，只有 interrupt handler 能终止它。两者缺一，`shutdown` 就可能永久挂在 `thread_join` 上。
- **`r<0` 必须按标志分类**：先看 `close_abort`，再看 `abort_requested`，最后才是真正的 JS 异常（`E_JS_EXCEPTION`）。interrupt 触发的 `r<0` 不得被当成普通 JS 异常。`E_CLOSE_ABORT` 只是 pump 的内部返回值，`shutdown` 对外返回 `NX_E_CLOSE_TIMEOUT`。
- **C 侧阻塞不可抢占**：interrupt 只在 JS 执行时触发；若宿主 Core handler 在 C 里死等，硬上限无法打断，`shutdown` 会等 join。这是宿主契约违规（Core 不得阻塞，§4.3），框架不掩盖，验收不构造该场景。
- 泵送期间如果 job 调用了 `__nx_call` 或 `__nx_reply`，只入 outbox，不会递归进入 turn 或 pump。用 `in_pump` 断言防止重入。
- 验收 `ACC-C-03` 的 10k 链允许跨 continuation turn，断言是“10k 个 continuation 全部执行且顺序正确”，不是“单 turn 内完成”。

**失控/关闭场景与最终码（不再声称单一来源）**：

| 场景 | 触发路径 | 标志/预算 | 最终码 | 统计键 | FATAL |
|---|---|---|---|---|---|
| 同步死循环（handler / eval） | interrupt 单脚本 50 ms | `abort_requested` | `E_JS_RUNAWAY` | `NX_STAT_JS_RUNAWAY` | 是 |
| 微任务洪水（无限 then 链） | `pump_jobs` 的 `episode_start + pump_budget_ms`（200 ms） | `abort_requested` | `E_JS_RUNAWAY` | `NX_STAT_PUMP_BUDGET` | 是 |
| 无限 `await`（Promise 永不 settle） | 不占 CPU；C pending 超时 | 不置 abort | `E_TIMEOUT`（发起方） | `NX_STAT_TIMEOUT` | 否 |
| handler 内 C 侧同步阻塞 | interrupt 无法进入 | 无 | 无（join 等待） | **不可观测**（无统计键） | 否；宿主契约违规 |
| 关闭 turn 内同步死循环 | interrupt 硬上限 500 + 200 ms（增量） | `close_abort` | `NX_E_CLOSE_TIMEOUT` | `NX_STAT_CLOSE_HARD_ABORT` | 否；继续关闭 |

> **已知限制（C 侧阻塞不可观测）**：没有独立定时器线程；JS 线程卡在 C handler 时，interrupt 也不会被调用，Bridge 无法知道进入时刻，因此**不提供** `NX_STAT_CORE_BLOCK`（永远为 0 的计数器只会误导）。宿主若需要，只能在 handler 内自行打点，并遵守“Core 不得阻塞”的契约。

### 5.3 Promise 拒绝

- 安装 `JS_SetHostPromiseRejectionTracker`；未处理的 rejection 记录 `NX_STAT_JS_EXCEPTIONS` 并通过 `$error` 通知 native（debug 下带 message，不带堆栈）。
- 不让未处理 rejection 终止进程（QuickJS 默认不终止）。

## 6. 超时与取消

- 每个 pending 有单调时钟截止时间；时钟默认 `clock_gettime(CLOCK_MONOTONIC)`，通过 shim 暴露。
- JS 线程在等待 inbox 前计算最近截止时间，用带超时的 `cond_wait`；醒来先处理到期项，再处理消息。
- 到期处理：
  - UI→JS：删除 pending → outbox 错误响应 `E_TIMEOUT`。
  - JS→UI：删除 pending → `__nx_on_response(id, false, E_TIMEOUT)` → JS reject。
- 晚到响应：查不到 pending → 丢弃 + `NX_STAT_DROPPED_LATE++`；不回错误。
- `$cancel`：通知，无响应；删除 pending 并立即以 `E_CANCELLED` 结束；已结束则忽略。
- 超时与取消都在 JS 线程串行处理，因此不会出现两个线程同时删除 pending 的竞态；竞争只发生在“对端响应”和“本端超时”两条入站消息之间，先到者删除，后到者丢弃。
- 没有独立定时器线程；超时精度取决于 `cond_wait` 的唤醒，通常 ≤ 10 ms 误差。验收用宽松阈值（见 acceptance.md）。

## 7. 关闭与销毁

### 7.1 两阶段

```
RUNNING --shutdown--> STOPPING --JS 线程退出--> DEAD --free--> FREED
```

**关闭顺序以本节为唯一权威**；design.md §7 与 protocol.md §12.6 只引用，不再各自描述。

`nexcore_shutdown`：

1. 原子设置 `stop_requested=true` 与 `state=STOPPING`；`abort_requested` 不动（只在失控时置位）；`nexcore_send` 从此返回 `NX_E_SHUTDOWN`。
2. `cond_signal` 唤醒 JS 线程与 drain 线程。
3. JS 线程完成当前 turn（不开始新 turn），置 `closing=true` 后执行关闭 turn，顺序**固定**为：
   1. 向 JS 发送 `$shutdown` 通知；
   2. JS 侧所有 pending Promise 以 `E_SHUTDOWN` 拒绝；
   3. **泵空微任务**（复用同一 pump，但使用独立的优雅预算 `shutdown_budget_ms`，默认 500 ms）；拒绝产生的 `.catch/.finally` reaction 在这一步执行；
   4. **优雅预算耗尽（`shutdown_budget_ms` = 500 ms）**：记录 `NX_STAT_CLOSE_BUDGET_EXCEEDED`，不再开始新 job；当前 job 若仍在同步执行，由 interrupt 在**硬上限** `shutdown_hard_limit_ms = shutdown_budget_ms + shutdown_hard_increment_ms`（500 + 200 = 700 ms）处抢占：置 `close_abort`、返回非 0，`JS_ExecutePendingJob` 以 `r<0` 返回。**若发生抢占**，记录 `NX_STAT_CLOSE_HARD_ABORT`，停止泵送、**继续关闭**（剩余 reaction 不执行；任何 Promise 不会被二次 settle），`shutdown` 返回 `NX_E_CLOSE_TIMEOUT`——不是 `E_JS_RUNAWAY`、不进入 FATAL。若当前 job 在 700 ms 前自行返回，则按正常关闭继续，返回值仍是 0。
   5. 清空 C pending 表；
   6. `JS_FreeContext` / `JS_FreeRuntime`；
   7. 标记 `js_done`，`cond_signal`。**JS 线程的所有可控退出路径（含 bootstrap 失败）都必须走到这一步**；不可控崩溃由第 4 步的异常路径兜底。
4. 等待与 join 约定（**3a 修复**）：
   - 由**非 JS 线程**调用（含 drain 线程）：只允许 `thread_join(js_thread)`，join 返回后才置 `joined=true` 并进入第 5 步。**不得用“等 `js_done`”替代 join**，否则 shutdown 可能在 JS 线程真正退出前返回。
   - 由 **JS 线程内部**调用（Core handler 等内部路径）：只置 `stop_requested` 并发信号，不能 join 自己，也**不能等待 `js_done`**（会自锁）；`shutdown` 立即返回 0，且**不设置 DEAD**。关闭 turn 在当前 JS 调用栈返回后由同一线程继续执行。
   - JS 线程内部发起时，join 由后续的非 JS `shutdown` 或 `free` 完成；在此之前 `free` 必须拒绝销毁。
   - **join 返回后必须检查 `js_done`（3a-a）**：若 `js_done==false`（JS 线程异常退出、未走步骤 3.7），不得静默停在 STOPPING。记录 `NX_STAT_JS_THREAD_ABORTED`，并输出 `last_turn_seq`、`last_job_id`、`last_method`（≤128 字节；只在 JS 线程写，join 后读，无竞态）作为原因线索；这些字段只能说明“最后执行到哪”，不能还原崩溃原因。ASAN 只报内存错误，**不报线程提前退出**，不能作为解释。随后调用 `finish_close(abnormal)` 进入 DEAD（不重复 `JS_Free*`），`shutdown` 返回 `NX_E_INTERNAL`；`free` 随后可用。
5. **DEAD 单点设置**：只有 `finish_close()` 写 `state=DEAD` 并 `cond_signal`，前置条件是 `joined==true`；若 `js_done==false`，先记 `NX_STAT_JS_THREAD_ABORTED` 再置 DEAD，并返回 `NX_E_INTERNAL`。正常路径返回 `NX_OK`。JS 线程内部路径不调用它。
6. 返回第 5 步的结果（正常 0；硬中断 `NX_E_CLOSE_TIMEOUT`；JS 异常退出 `NX_E_INTERNAL`）。

**FATAL 的释放时机（P1-b）**：FATAL 只置 `abort_requested`/`stop_requested` 与 `state=FATAL`，**不得**在 `JS_ExecutePendingJob` 尚未返回时释放 runtime。释放必须发生在“job 已返回、JS 栈已展开、回到 turn 边界”之后；若 FATAL 发生在 job 执行中途，当前 job 的返回值丢弃，等它返回（或由 interrupt 截断）后再走释放。释放完成才置 `js_done`；DEAD 仍只由 `finish_close()` 设置。

**四类在途对象的收尾**：

| 对象 | shutdown 时处理 | 调用方看到 |
|---|---|---|
| JS pending（两个方向） | 全部以 `E_SHUTDOWN` 拒绝；随后按步骤 3.3 泵送，`.catch` 有机会执行（受 3.4 预算约束） | JS Promise reject；native 侧由平台用自己的 pending 表拒绝，Bridge 不再回响应 |
| outbox 中已生成、未取走的响应 | 丢弃；不调用 sink；`NX_STAT_DROPPED_SHUTDOWN++`（每条一个） | 平台 pending 在 `shutdown` 返回后由 transport 统一以 `E_SHUTDOWN` 结束 |
| outbox 中未投递的事件 | 丢弃；不调用 sink；`NX_STAT_DROPPED_SHUTDOWN++`（每条一个） | 平台不再收到事件 |
| inbox 中已接受、未处理的请求 | 丢弃；不执行 handler、不产生响应；`NX_STAT_DROPPED_SHUTDOWN++`（每条一个） | 平台侧 pending 同样以 `E_SHUTDOWN` 结束 |

> 计数口径：`NX_STAT_DROPPED_SHUTDOWN` 是**三类对象之和**（响应 + 事件 + 未处理请求），每条消息加一。ACC-C-05 的 `==300` 即 100 + 100 + 100。

**硬保证**：`nexcore_shutdown` 返回后，Bridge 不再调用 sink；`free` 返回后不再触碰任何句柄资源。

`nexcore_free`：

- **禁止在 JS 线程内调用（3a-c）**：handle 保存 `js_thread_id`，进入函数先比对当前线程；相等直接返回 `NX_E_REENTRANT`，不取锁、不 join。否则会 `thread_join(self)`。
- 在**生命周期锁内**检查：`drain_active==0` 且 `stop_requested==true`（否则 `NX_E_BUSY`；不允许未 `shutdown` 就 `free`）。
- 若 `joined==false`（只可能来自 JS 线程内部发起的关闭，或 FATAL 后未显式 shutdown）：先释放锁执行 `thread_join(js_thread)`，再回锁调用 `finish_close()`；**join 完成后才断言 `state==DEAD`**。禁止在 `joined==false` 时销毁。
- **FATAL 的收尾（3a-b）**：进入 FATAL 时同时置 `stop_requested=true`；JS 线程**只在 job 返回、回到 turn 边界后**才释放 runtime 并置 `js_done`（见上）。`free` 允许在 FATAL 后直接调用：按上一条 join → `finish_close()`（异常路径记 `NX_STAT_JS_THREAD_ABORTED`）→ DEAD → 销毁。**DEAD 仍然只由 `finish_close()` 设置**，FATAL 不直接写 DEAD。
- 在同一把锁内销毁锁/条件变量、释放队列、释放 handle，消除与 drain 进入的 TOCTOU。
- 之后任何调用都是调用方 bug；生成绑定必须保证顺序。

**JS 线程内发起关闭的正确用法（唯一写法）**：在 JS handler/Core handler 里只调用 `nexcore_shutdown(h)` 并**立即 return**，不要调用 `free`、不要等待关闭完成；由外部线程在 drain 线程退出后调用 `nexcore_free(h)`。生成绑定的注释与平台接入文档必须原样写出这条，禁止在 JS handler 内 `free`。

### 7.2 晚到的回调

| 场景 | 处理 |
|---|---|
| native 在 `shutdown` 后发送响应 | `nexcore_send` 检查状态，返回 `NX_E_SHUTDOWN`；不进入队列 |
| JS 的 Promise 在 shutdown 时仍未完成 | 关闭 turn 中统一 reject `E_SHUTDOWN`；不等待 native |
| native 的 pending 在 shutdown 时仍未完成 | 平台 transport 在 `shutdown` 返回后统一以 `E_SHUTDOWN` 结束；Bridge 不回调 |
| outbox 中未投递的事件/响应 | 丢弃；累加 `NX_STAT_DROPPED_SHUTDOWN`；`shutdown` 返回后不再调用 sink |
| inbox 中已接受、未处理的请求 | 丢弃；不执行 handler、不产生响应；平台 pending 自行拒绝 |
| 优雅预算耗尽（500 ms） | 记录 `NX_STAT_CLOSE_BUDGET_EXCEEDED`；不再开始新 job；当前 job 由硬上限抢占后继续关闭；剩余 promise reaction 不执行，不会二次 settle |
| 关闭硬上限（500 + 200 ms 增量 = 700 ms） | 置 `close_abort`；interrupt 返回非 0；`shutdown` 返回 `NX_E_CLOSE_TIMEOUT`；不进入 FATAL；`free` 仍可用 |
| JS 线程异常退出（join 后 `js_done==false`） | 记录 `NX_STAT_JS_THREAD_ABORTED`；`finish_close(abnormal)` 置 DEAD；`shutdown` 返回 `NX_E_INTERNAL`；`free` 可用 |
| drain 线程在 shutdown 后仍阻塞 | `nexcore_drain` 被唤醒并返回 `E_SHUTDOWN`；transport 退出线程 |
| `free` 时 drain 仍在执行 | 返回 `NX_E_BUSY`；transport 必须等待 drain 返回后重试 |
| JS 线程已退出，native 仍持有 handle | 除 `free` 外全部返回 `E_SHUTDOWN`；`stat` 返回最后快照 |
| FATAL 后调用 drain | 首次返回 `NX_E_INTERNAL`，之后返回 `E_SHUTDOWN` |
| FATAL 后调用 free | FATAL 已置 `stop_requested`；`free` join → `finish_close()` → DEAD → 销毁；不需要额外的 `shutdown` |
| FATAL 发生在 job 执行中途 | 只置标志；当前 job 返回（或 interrupt 截断）到 turn 边界后才 `JS_Free*` 并置 `js_done`；不在栈未展开时释放 |
| 在 JS 线程内调用 free | 返回 `NX_E_REENTRANT`，不 join、不销毁 |

### 7.3 为什么不做 generation/session

- 两阶段关闭 + 状态检查已经覆盖“晚到响应”。
- 每个 handle 有独立队列；旧 handle 的消息不会进入新 handle。
- 真正的 use-after-free 只能由调用方在 `free` 后继续调用造成；这是 API 契约问题，生成绑定负责阻止，不用运行时魔法掩盖。

## 8. 竞态清单

| # | 竞态 | 结果 | 处理 | 测试 |
|---|---|---|---|---|
| RC-01 | 响应与超时同时到达 | 双重 settle | JS 线程串行；删除 pending 是唯一入口；后到者丢弃 | 1 ms 超时 + 真实响应 |
| RC-02 | 取消与响应同时到达 | 双重 settle | 同上；先删除者生效 | `$cancel` 与响应竞跑 1000 次 |
| RC-03 | shutdown 与 send 同时 | 队列中残留 | `send` 检查状态后再入队；STOPPING 直接拒绝 | 多线程 send + shutdown |
| RC-04 | shutdown 与 drain 同时 | drain 卡住 | `shutdown` 唤醒 drain；drain 空队列返回 `E_SHUTDOWN` | 阻塞 drain + shutdown |
| RC-05 | sink 内调用 send | 死锁 | sink 在锁外调用；send 只入队 | sink 内回发响应 |
| RC-06 | sink 内调用 shutdown | 重入/自 join | 返回 `E_REENTRANT` | 断言 |
| RC-07 | 两个线程 drain | 消息乱序/重复 | CAS 标记 drain 所有者；第二个返回 `E_THREAD` | 并发 drain |
| RC-08 | free 时 drain 仍阻塞、在 JS 线程内 free、或 FATAL 未收尾 | use-after-free / `thread_join(self)` / 卡在非 DEAD | 生命周期锁内检查 `drain_active==0` 且已 shutdown；JS 线程内调用直接 `NX_E_REENTRANT`；`joined==false` 先 join 再 `finish_close()`；`js_done==false` 走 abnormal 置 DEAD；FATAL 也经 `finish_close()` | 阻塞 drain + free；JS 线程内 free；FATAL 后 free；JS 崩溃后 free |
| RC-09 | 多线程 send 填满 inbox | 部分失败 | 锁内检查容量；失败返回 `E_BACKPRESSURE`；不丢已接受消息 | 8 线程 × 10k |
| RC-10 | outbox 事件队列满 | 丢响应 | 响应/事件分队列；只丢事件 | 洪水事件 + 响应 |
| RC-11 | JS handler 在 emit 中退订 | 访问已释放订阅 | 快照 + active 标记；调用前检查 | 退订自己/别人 |
| RC-12 | JS 失控 Promise 循环 | JS 线程卡死 | 10k job 只是让出配额（YIELD，续 turn 继续）；连续泵送 200 ms 或单脚本 50 ms 由 interrupt handler 判定，置 `abort_requested` 并触发 fatal shutdown | 无限 then 链 fatal；10k 有限链必须全部完成（ACC-C-03） |
| RC-13 | Core 在 JS 调用栈内 emit | 重入 | core 事件入独立队列，turn 末尾投递 | core emit 后立即返回 |
| RC-14 | bootstrap 抛异常 | create 挂起/泄漏 | 初始化在 JS 线程完成，失败上报并 join；释放 runtime | 坏 bootstrap |
| RC-15 | JS OOM | 进程崩溃 | `JS_SetMemoryLimit`；OOM 转为 JS 异常；上报并关闭 | 分配炸弹 |
| RC-16 | 关闭 turn 内单 job 不可抢占 | `shutdown` 永久挂起（P0） | grace 期（`shutdown_budget_ms` = 500 ms）interrupt 返回 0；硬上限（`shutdown_hard_limit_ms` = 500 + `shutdown_hard_increment_ms` = 700 ms）置 `close_abort` 并返回非 0 抢占当前 job；`r<0` 先查 `close_abort`，返回 `NX_E_CLOSE_TIMEOUT`，不报 `E_JS_RUNAWAY` | 关闭 turn 的 `.catch` 内放同步死循环；断言在 700 ms + `shutdown_schedule_slack_ms`（200 ms，仅验收）内 `shutdown` 返回且句柄可 `free` |

## 9. 可观测性

- `nexcore_stat` 暴露队列深度、pending 数、丢包数、turn/job 数、异常数。
- debug 模式可输出结构化 trace：`ts, direction, id, method, turn_no, thread, event`；**不记录 payload**，避免 PII。
- 每个错误响应带 `code`；日志包含 `method` 与 `id`，便于把一次调用串起来。
- 线程断言：`nx_assert_js_thread`、`nx_assert_drain_thread`；debug 下失败即 abort，release 下返回 `E_THREAD`。

## 10. JS 线程伪代码

```c
static void *js_thread_main(void *arg) {
    NxCore *c = arg;
    c->rt = JS_NewRuntime();
    JS_SetMemoryLimit(c->rt, c->cfg.memory_limit);
    JS_SetMaxStackSize(c->rt, c->cfg.stack_limit);
    JS_SetInterruptHandler(c->rt, nx_interrupt, c);
    c->ctx = JS_NewContext(c->rt);
    JS_SetContextOpaque(c->ctx, c);
    JS_UpdateStackTop(c->rt);

    if (nx_bootstrap(c) != NX_OK) {         /* eval bootstrap + app script */
        nx_signal_init(c, NX_E_INTERNAL);
        return NULL;
    }
    nx_signal_init(c, NX_OK);

    while (nx_state(c) == NX_RUNNING) {
        nx_process_timeouts(c);
        if (nx_inbox_empty(c)) {
            nx_wait_inbox(c, nx_next_deadline(c));  /* cond_wait with timeout */
            continue;
        }
        NxMsg *m = nx_inbox_pop(c);
        nx_turn(c, m);
        nx_free_msg(m);
    }
    nx_shutdown_turn(c);
    JS_FreeContext(c->ctx);
    JS_FreeRuntime(c->rt);
    nx_signal_js_done(c);
    return NULL;
}
```

## 11. drain 线程伪代码

```c
/* 平台 transport 的线程 */
for (;;) {
    int32_t n = 0;
    int32_t rc = nexcore_drain(h, -1, &n);
    if (rc == NX_E_SHUTDOWN) break;
    if (rc != NX_OK) { /* 记录并退出 */ break; }
    /* sink 已在 drain 内逐条调用；平台在 sink 内复制并调度到 UI */
}
```

## 12. 禁止事项

1. 在 JS 线程调用平台 API 或阻塞 I/O。
2. 在 drain 线程调用 `JS_*`。
3. 在 sink 内 `shutdown/free`。
4. 把 `JSValue` 存进 C 结构体并在其他 turn 使用。
5. 在 JS 执行期间泵微任务。
6. 用“泵一轮”替代泵空（会破坏 Promise 链顺序）。
7. 用 `sleep`/轮询代替条件变量。
8. 在锁内调用 sink、JS、Core 或分配大内存。
9. 让 UI 主线程阻塞等待 JS。
10. 依赖事件顺序推断请求完成。
