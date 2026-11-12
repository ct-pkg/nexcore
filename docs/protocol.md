# 协议 v1

本文定义**宿主 UI ↔ 宿主 JS** 边界上的 JSON 信封、错误、订阅/发布语义、超时/取消、背压与版本规则。协议是框架交付物，但**方法名集合、事件名与业务字段由宿主 Schema 定义**；框架只保证信封合法、路由正确、错误可分支，不解释业务含义。JS↔Core 不走这条协议（见 §7），这是理解全文的前提。

## 1. 设计原则

1. **一种信封**：请求、成功响应、错误响应、通知，四种形态，没有第五种。
2. **一个解析器**：JSON 只由 QuickJS 解析（`JS_ParseJSON`）和序列化（`JS_JSONStringify`）；CT 不实现 JSON 解析器。
3. **方法名是白名单**：只有 Schema 注册过的方法能通过；不存在动态方法名、反射调用、`eval`。
4. **幂等边界清晰**：`id` 只负责匹配，不负责去重；重试必须换新 id。
5. **失败是一等公民**：超时、取消、关闭、背压、JS 异常、平台错误都有稳定错误码。
6. **两个方向共享信封，不共享语义**：UI→JS 与 JS→UI 在生命周期、超时归属、订阅方式上不对称，§8 逐条列出。

## 2. 信封

### 2.1 请求

```json
{"id": 7, "method": "ui.toast", "params": {"text": "saved", "duration_ms": 1500}}
```

| 字段 | 类型 | 必填 | 约束 |
|---|---|---|---|
| `id` | 整数 | 是 | `1..2^32-1`；同一方向内单调递增；不得复用未完成的 id |
| `method` | 字符串 | 是 | `^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$`，≤128 字节；`$` 前缀保留 |
| `params` | 对象 | 是 | 必须是 JSON object；无参数写 `{}`；不得为 `null`/数组 |

### 2.2 成功响应

```json
{"id": 7, "result": null}
```

- `id` 必须与请求一致。
- `result` 可以是任意 JSON 值；`void` 用 `null`。
- 成功响应不得同时带 `error`。

### 2.3 错误响应

```json
{"id": 7, "error": {"code": "E_TIMEOUT", "message": "ui.toast timed out after 1000 ms", "retryable": true, "data": {"elapsed_ms": 1000}}}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `code` | 字符串 | 是 | 稳定错误码，见 §4；调用方只按它分支 |
| `message` | 字符串 | 是 | 人类可读，禁止被程序解析；不含密钥/路径 |
| `retryable` | 布尔 | 否 | 默认 `false`；只是建议，协议不自动重试 |
| `data` | 任意 JSON | 否 | 结构化细节；debug 下可含 `stack` |

错误响应不得同时带 `result`。

### 2.4 通知

```json
{"method": "$emit", "params": {"topic": "cart.changed", "payload": {"lines": []}}}
```

- 没有 `id`，**没有响应**。
- 通知的处理错误通过 `$error` 通知与统计上报，不回复给发送方。
- 需要确认的调用必须用请求，不能用通知。

## 3. 字段与值约束

### 3.1 id

- 由发起方向分配：UI→JS 由平台绑定分配；JS→UI 由 JS bootstrap 分配。
- **id 按方向独立**：`(direction, id)` 才是唯一键；UI→JS 的 7 与 JS→UI 的 7 可以同时存在。任何引用 id 的控制消息必须带 `direction`。
- 从 1 开始；0 保留；`2^32-1` 之后回绕，回绕时跳过仍在 pending 的 id（实际业务量下不会发生，但实现必须显式处理）。
- 重复的未完成 id → `E_DUPLICATE_ID`，请求被拒绝，不进入 pending。
- 响应中的未知 id（已超时/已取消/已关闭）→ 丢弃并累加 `NX_STAT_DROPPED_LATE`，**不回错误**，避免错误风暴。

### 3.2 method

- 小写 ASCII；点分命名空间；至少两段。
- 保留前缀 `$` 只用于 §5 的内置方法；业务方法不得以 `$` 开头。
- 大小写敏感；不做模糊匹配、不做前缀匹配。
- 方法表在编译期生成；运行时只做精确字符串比较（C 侧 registry / JS 对象属性）。

### 3.3 params 与 result

- UTF-8；不允许孤立代理项；不允许 `NaN`/`Infinity`。
- 最大 1 MiB（可配置 `max_message_bytes`）；最大嵌套深度 32。
- 数值：
  - `int32` / `float64` 用 JSON number；
  - `int64` 也用 JSON number，但必须在 `[-9007199254740991, 9007199254740991]`（±(2^53-1)）内；超出 → `E_INVALID_PARAMS`。金额用最小货币单位的整数，不使用浮点。
  - 需要超过 2^53 的标识符用 `string` 类型，不用 `int64`。
- `bytes` 用 Base64 字符串；大二进制（>64 KiB）不建议走本协议（Q-04）。
- 重复键：QuickJS `JSON.parse` 采用后值覆盖；协议不额外报错，Schema 校验只看到最终值。不要依赖重复键表达语义。
- 未知字段：忽略（向前兼容）；未知枚举值：默认 `E_INVALID_PARAMS`，Schema 标 `open: true` 时保留原值。

## 4. 错误码

本节是**协议域**的 `E_*` 字符串码；ABI 域是 `NX_E_*` 整数码（[design.md](design.md) §6.3）。两套码不复用数值、不互相替代：跨 ABI 的失败看 `int32_t`，协议信封里的失败看 `code`。

| code | 数值（日志用） | 可重试 | 含义 |
|---|---|---|---|
| `E_PARSE` | -32700 | 否 | JSON 解析失败；通常以 `$error` 通知出现 |
| `E_INVALID_REQUEST` | -32600 | 否 | 信封不合法（缺 id/method、id=0、result+error 同时出现） |
| `E_NO_METHOD` | -32601 | 否 | 方法未注册或方向不允许 |
| `E_INVALID_PARAMS` | -32602 | 否 | 参数缺失、类型错、超限、枚举非法 |
| `E_INTERNAL` | -32603 | 是 | 桥接内部错误（OOM、锁失败） |
| `E_TIMEOUT` | -32001 | 是 | 超过方法超时 |
| `E_CANCELLED` | -32002 | 否 | 被 `$cancel` 取消；取消是终态 |
| `E_SHUTDOWN` | -32003 | 否 | 运行时已关闭 |
| `E_BACKPRESSURE` | -32004 | 是 | 队列/待处理表满 |
| `E_PAYLOAD_TOO_LARGE` | -32005 | 否 | 超过消息大小限制 |
| `E_UNSUPPORTED` | -32006 | 否 | 协议/ABI 版本不支持 |
| `E_JS_EXCEPTION` | -32007 | 否 | JS handler 抛异常 |
| `E_JS_RUNAWAY` | -32008 | 否 | 微任务/脚本超过 turn 预算，被中断 |
| `E_SCHEMA_MISMATCH` | -32009 | 否 | schema hash 不一致 |
| `E_DUPLICATE_ID` | -32010 | 否 | id 已被未完成请求占用 |
| `E_RESULT_UNSERIALIZABLE` | -32011 | 否 | handler 返回值无法 JSON 序列化（函数、Symbol、BigInt、循环引用） |
| `E_HANDLER_EXCEPTION` | -32012 | 否 | 平台 handler 抛错/返回错误 |
| `E_FORBIDDEN` | -32013 | 否 | 方向/白名单不允许（如 UI 调 core-only 方法） |
| `E_EMIT_DEPTH` | -32014 | 否 | 重入 emit 超过深度上限 |

> `-32011` 原先分配给 `E_NOT_SUBSCRIBED`，该码已删除：`$unsubscribe` 对未知 subId 一律幂等忽略，见 §6.3。错误码数值不复用，避免旧日志误读。

规则：

- 调用方按 `code` 分支；`message` 只用于日志/展示。
- 新错误码只增不改；删除/改语义需要协议大版本。
- `E_INTERNAL`、`E_JS_EXCEPTION` 的 `data` 可以带 `detail`，但生产环境不得带堆栈/绝对路径/密钥。

## 5. 保留方法

所有保留方法都以 `$` 开头，由 Bridge/JS bootstrap 实现，业务不得覆盖。

| 方法 | 形态 | params | result | 说明 |
|---|---|---|---|---|
| `$subscribe` | 请求 | `{"topic": string}` | `{"subId": uint32}` | native 订阅 JS 事件 |
| `$unsubscribe` | 请求 | `{"subId": uint32}` | `null` | native 取消订阅 |
| `$emit` | 通知 | `{"topic": string, "payload": any}` | 无 | 任一方向发布事件 |
| `$cancel` | 通知 | `{"direction":"ui2js"\|"js2ui","id": uint32}` | 无 | 取消对端 pending 请求，尽力而为；`direction` 指向被取消请求的发起方向 |
| `$shutdown` | 通知 | `{"reason": string}` | 无 | Bridge 在关闭 turn 通知 JS；JS 不得回复 |
| `$error` | 通知 | `{"code": string, "message": string, "topic"?: string}` | 无 | 通知处理失败/背压/解析失败 |
| `$ping` | 请求 | `{}` | `{"t": int64}` | 健康检查，仅 debug/测试 |

`$subscribe` 的 `subId` 由 JS 侧分配，native 保存映射；`$unsubscribe` 只影响事件，不影响该订阅者发起的在途请求。

`$cancel` 的 `direction` 缺失或不是 `ui2js`/`js2ui` → 回 `E_INVALID_PARAMS`，**不猜方向**；两个方向恰好同号时，缺方向的取消属于歧义请求，宁可报错也不能取消错对象。

## 6. 订阅/发布语义

### 6.1 订阅

**JS 侧：**

```js
const sub = nexcore.subscribe("cart.changed", (payload) => { ... });
nexcore.unsubscribe(sub);
```

- 本地同步注册，立即返回 `subId`（uint32），不经过协议。
- 同一个 handler 可以订阅多个 topic；每次调用都是独立订阅。
- `unsubscribe` 幂等：重复调用不报错；未知 id 忽略并返回 `null`。没有 `E_NOT_SUBSCRIBED` 这个码。
- handler 在 JS 线程同步调用；抛异常被捕获，不影响其他订阅者，并产生 `$error`。

**Native 侧：**

```swift
let sub = client.subscribe("cart.changed") { payload in ... }
```

- 发送 `$subscribe` 请求；JS 侧登记一个 native 订阅并返回 `subId`。
- 之后 JS `emit` 匹配 topic 时，Bridge 把事件放入 outbox，drain 线程调用 native handler。
- `unsubscribe` 发送 `$unsubscribe` 请求；在途事件可能已经在 outbox 中，native 侧必须用 `subId` 再过滤一次（见 protocol.md §6.3）。

### 6.2 发布

**唯一事件源是 JS。** 两个方向的 `emit` 最终都在 JS 线程完成扇出：

- JS `nexcore.emit(topic, payload)`：当前 turn 内同步依次调用 JS 订阅者；然后把事件放入 outbox 给 native 订阅者。
- Native `client.emit(topic, payload)`：发送 `$emit` 通知；JS 线程收到后执行同样的扇出。
- 这样只有一套订阅表、一套顺序规则，不会出现“native 发的事件 native 自己没收到”的分叉。

### 6.3 快照与退订

一次 `emit` 开始时对订阅者列表做**快照**：

1. emit 期间新增的订阅不收到本次事件。
2. emit 期间 `unsubscribe` 会把订阅标记为 inactive；在调用每个 handler 前检查该标记，因此尚未执行的 handler 会被跳过。
3. 正在执行的 handler 不被打断，执行完返回。
4. 退订不取消该订阅者发起的在途请求；需要取消时显式 `$cancel`。

### 6.4 顺序与投递保证

- **每个 emitter 内 FIFO**：同一 emitter 连续 emit 的顺序，订阅者按该顺序看到。
- **跨 emitter 无全序**：不同来源的事件可以交错。
- **事件与响应无相对顺序保证**：一个请求的响应可能晚于/早于另一个事件；不要用事件顺序推断请求完成。
- 默认 `delivery: reliable`：在队列容量内不丢、不重、保序；超过容量时丢弃**事件**，并按**丢弃批次**计数：每次溢出到队列恢复之间最多产生一条 `$error(E_BACKPRESSURE)`，字段 `dropped` 给出该批次丢弃数。绝不丢响应。
- `delivery: latest`：同一 topic 只保留最新 payload；旧的未投递事件被覆盖。用于高频 UI 状态（滚动位置、输入预览），不用于业务事实。
- 不提供 at-least-once；没有持久化、没有重放。需要可靠业务事实时，用请求/响应 + 业务侧幂等键。

### 6.5 handler 失败

- JS handler 抛异常：捕获，继续调用其他订阅者；发送 `$error`（含 topic，不含堆栈）；`NX_STAT_JS_EXCEPTIONS++`。
- Native handler 抛错/返回错误：生成绑定捕获，转为 `$error` 通知给 JS；继续其他订阅者。
- 单个订阅者失败不得影响同一事件的其他订阅者，也不得让 emit 调用本身失败（emit 是通知，没有返回错误通道）。

## 7. JS↔Core：不在协议上

Core 方法是**同进程、同线程**的 C 调用，不经过 JSON、id、pending map：

```js
// 生成物
export const cart = {
  async add(line) {
    return __nx_core_call("cart.add", [line]); // C 函数，JS 线程同步执行
  }
};
```

- 生成包装函数直接调用 C，读取 `JSValue` 参数（`JS_ToInt32`、`JS_GetPropertyStr`），返回 `JSValue`。
- 用 `async` 包装，调用方统一拿到 Promise；执行本身是同步的。
- 没有 id、没有超时、没有 `$cancel`；Core 必须快速、无阻塞。慢操作应放到平台/后台线程，通过 JS→UI 的异步方法实现。
- Core 错误用 `int32_t` 状态码返回；包装函数转成带 `code` 属性的 JS 异常。
- Core 不能反向请求 JS；只能发布事件（Bridge 提供 `nx_emit_from_core`，在后续 turn 投递），避免重入与死锁。

## 8. 两个方向是否对称

**信封对称，语义不对称。** 逐条差异：

| 维度 | UI→JS | JS→UI |
|---|---|---|
| 发起者 | native | JS |
| 入队 API | `nexcore_send` | `__nx_call`（C 函数） |
| 解析/执行线程 | JS 线程 | drain 线程（native），再按平台调度到 UI |
| 响应路径 | outbox → drain → sink | `nexcore_send` → JS turn |
| 超时归属 | Bridge pending 表（UI→JS） | Bridge pending 表（JS→UI） |
| 取消方式 | native 发 `$cancel`，`direction=js2ui` | JS 发 `$cancel`，`direction=ui2js` |
| 背压表现 | `nexcore_send` 返回 `E_BACKPRESSURE` | JS Promise 以 `E_BACKPRESSURE` 拒绝 |
| 订阅 | native 发 `$subscribe` 请求 | JS 本地同步注册 |
| 生命周期权力 | native 可 `shutdown/free` | JS 不能；只能被通知 `$shutdown` |
| 错误域 | JS 异常 → 协议错误码 | 平台错误 → 协议错误码 |
| Core 访问 | 禁止（必须经 JS） | 直接同步调用（§7） |
| 线程保证 | handler 只在 JS 线程 | handler 在 drain 线程，平台绑定负责切到 UI |

结论：不能因为“信封一样”就假设两边可以复用同一套 pending/超时/订阅实现。平台绑定与 JS bootstrap 各自实现自己的一半，Bridge 只负责跨线程搬运、pending 超时表和关闭。

## 9. 超时、取消、重试

### 9.1 超时

- 每个方法有超时：Schema 的 `timeout_ms` 覆盖配置默认值；`0` 表示不超时（必须显式声明）。
- 计时从消息被接受开始：UI→JS 从 `nexcore_send` 返回 0 起；JS→UI 从 `__nx_call` 返回 0 起（不是从 outbox 被 drain 取走起）。
- 超时后：从 pending 表移除，向发起方返回 `E_TIMEOUT`，`retryable=true`；晚到的响应按 §3.1 丢弃。
- 超时不等于取消：对端 handler 可能仍在执行；需要停止时另发 `$cancel`。

### 9.2 取消

- `$cancel` 是通知，尽力而为；必须带 `direction` 与原请求 id。`(direction,id)` 唯一确定被取消对象，两个方向的同号 id 不会互相影响。
- 收到 `$cancel` 时：按 `direction` 选择 pending 表；若存在，移除并以 `E_CANCELLED` 结束原请求；若已结束，忽略。
- 取消后原 handler 的响应到达 → 丢弃并计数，不产生第二次 settle。
- 取消不保证对端停止执行；Core/平台 handler 必须自己检查协作式取消标志（Schema 标 `cancellable: true` 时生成对应参数）。

### 9.3 重试

- 协议不自动重试。
- 重试必须换新 id；同一个 id 不能用于两次请求。
- `retryable` 只是建议；有副作用的操作应由业务提供幂等键（放在 params 中，Schema 标 `idempotency_key: true`）。

## 10. 背压与限额

| 资源 | 默认上限 | 超限行为 |
|---|---|---|
| 单条消息 | 1 MiB | `nexcore_send` 返回 `E_PAYLOAD_TOO_LARGE`；JS 侧抛 `E_PAYLOAD_TOO_LARGE` |
| 嵌套深度 | 32 | `E_INVALID_PARAMS` |
| 方法名 | 128 字节 | `E_INVALID_REQUEST` |
| inbox 队列 | 4096 条 / 8 MiB | `nexcore_send` 返回 `E_BACKPRESSURE` |
| outbox 事件队列 | 4096 条 / 8 MiB | 丢弃事件；每个溢出批次发一条 `$error(E_BACKPRESSURE)`（带 `dropped` 计数）；响应单独队列，不丢 |
| pending（每方向） | 1024 | 新请求立即 `E_BACKPRESSURE`；不进入 pending |
| 重入 emit 深度 | 8 | `E_EMIT_DEPTH` |

响应队列与事件队列分开：事件可丢，响应不可丢。响应队列满属于内部故障，`E_INTERNAL` 并触发 shutdown。

## 11. 版本与兼容

- 协议版本在 `NexCoreConfig.protocol_version` 中传递；v1 只接受 `1`，否则 `E_UNSUPPORTED`。
- Schema hash 在 `create` 时校验；native、C 注册表、JS bootstrap 三处必须一致，否则 `nexcore_create` 返回 `NX_E_SCHEMA_MISMATCH`（ABI 域），创建失败。协议域 `E_SCHEMA_MISMATCH` 只用于创建成功后对端经 `$error` 上报的 hash 不一致，Bridge 不主动发起。
- 兼容规则：
  - **增量**：新增方法/事件、新增可选字段、新增错误码 → 不升协议版本，只更新 schema hash。
  - **破坏**：删除/重命名方法或字段、改变类型/方向/超时语义 → 升协议大版本，三端同版本发布。
- 未知字段忽略；未知方法报错；未知枚举值默认报错（`open: true` 除外）。
- 不支持批量消息；一次 ABI 调用一条 JSON。

## 12. 示例序列

### 12.1 UI→JS 成功

```
UI:   {"id":1,"method":"app.refresh","params":{"force":true}}
JS:   {"id":1,"result":{"updated":3}}
```

### 12.2 JS→UI 成功

```
JS:   {"id":41,"method":"ui.toast","params":{"text":"saved","duration_ms":1500}}
UI:   {"id":41,"result":null}
```

### 12.3 超时

```
UI:   {"id":2,"method":"ui.pick_file","params":{}}
      ... 1000 ms 后 Bridge 移除 pending ...
UI:   {"id":2,"error":{"code":"E_TIMEOUT","message":"ui.pick_file timed out","retryable":true}}
      ... 之后真正的响应到达 ...
      {"id":2,"result":{"path":"/tmp/a"}}   // 丢弃并计数
```

### 12.4 取消

```
JS:   {"id":51,"method":"ui.long_task","params":{}}
JS:   {"method":"$cancel","params":{"direction":"js2ui","id":51}}
UI:   {"id":51,"error":{"code":"E_CANCELLED","message":"cancelled","retryable":false}}
```

### 12.5 订阅/发布

```
UI:   {"id":3,"method":"$subscribe","params":{"topic":"cart.changed"}}
JS:   {"id":3,"result":{"subId":7}}
JS:   {"method":"$emit","params":{"topic":"cart.changed","payload":{"lines":[]}}}
UI:   {"method":"$emit","params":{"topic":"cart.changed","payload":{...},"subId":7}}  // Bridge 注入 subId
```

### 12.6 关闭

关闭的**步骤顺序**只在 [async-threading.md](async-threading.md) §7.1 定义，本节不重复。协议侧只固定可见行为：

```
Bridge → JS: {"method":"$shutdown","params":{"reason":"app_terminate"}}
JS 侧所有 pending Promise 以 E_SHUTDOWN 拒绝（随后泵空，.catch 有机会执行）
优雅预算 500 ms；超过硬上限（+200 ms）时当前 job 被 interrupt 抢占，剩余 reaction 不再执行
硬中断不产生 E_JS_RUNAWAY；ABI 层返回 NX_E_CLOSE_TIMEOUT，协议侧不承诺 .catch 一定跑完
shutdown 返回后不再有 sink 回调；inbox/outbox 中未处理项按 async §7.1 丢弃
drain 线程下一次 drain 得到 E_SHUTDOWN 后退出
```

## 13. 不合规处理

| 情况 | 处理 |
|---|---|
| JSON 解析失败 | `$error(E_PARSE)` 通知；无 id 时不回复；计数 |
| 缺 `method` | 有 id 时立即回 `E_INVALID_REQUEST` 错误响应（不让发起方挂起）；无 id 时发 `$error` |
| `id=0` 或非整数 | `E_INVALID_REQUEST`；id 不可用则走 `$error` |
| 响应同时有 `result` 和 `error` | 不回 `$error`（无法确定收件人）：丢弃 + 计数 `NX_STAT_MALFORMED`；若 id 可解析，向该 id 的发起方回 `E_INVALID_REQUEST` |
| 响应 id 未知 | 丢弃 + `NX_STAT_DROPPED_LATE`，不回复 |
| 方法未注册 | `E_NO_METHOD`；不进入 outbox |
| 方向不允许 | `E_FORBIDDEN`（例如 UI 直接调 core-only 方法） |
| 超过大小/深度 | `E_PAYLOAD_TOO_LARGE` / `E_INVALID_PARAMS` |
| 通知处理失败 | `$error` 通知 + 计数；不尝试回复 |
| handler 返回值不可序列化 | 向该请求回 `E_RESULT_UNSERIALIZABLE`；不得把 `undefined` 当成功 |
| 同一个 id 重复 | `E_DUPLICATE_ID`；原请求不受影响 |
