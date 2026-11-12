# 设计文档第四轮复看

- 复核对象：`docs/` v0.6（关闭硬上限 + 3a 补全 + D-15 论据收敛 + 场景表）
- 复核方式：只读 `docs/`；本轮不新增实验
- 结论：**P0 清零**。剩余 P1 4 条、P2 9 条
- 一句话：`close_abort` 的置位链**成立**，因为置位者是 QuickJS 回调的 interrupt handler，不是“卡住之后才轮到的 JS 线程主流程”；但伪代码与 §7.1 有一处冲突，FATAL 释放时机还差一句

---

## 一、`close_abort` 由谁置位：修法成立

先把结论说死：**上一轮那条 P0 修法成立，P0 清零。** 你担心的“置位者如果是 JS 线程自己，它正卡在 `JS_ExecutePendingJob` 里，永远走不到置位那一行”，对 `JS_SetInterruptHandler` 不适用。

原因：interrupt handler 不是 JS 线程的主流程，而是 QuickJS 在**执行字节码期间同步回调**的一个 C 函数。`JS_ExecutePendingJob` 内部运行 JS 字节码时，QuickJS 会周期性调用它；它就在 JS 线程上、在 job 内部、在 `while(true){}` 的循环体之间被调用。因此：

- 置位那一行不是“job 返回后才执行”，而是“job 还在跑的时候由一个被 QuickJS 重入的 C 回调执行”；
- 这条机制本来就是用来打断 `while(true){}` 的，`abort_requested` 已经在用同一套；
- 文档 L196 已写明“它在 QuickJS 执行 JS 字节码期间被周期性回调，因此能抢占单个不返回的 job”，L199 又写明“两者缺一，shutdown 就可能永久挂”，说明设计者理解正确。

逐条回答：

1. **置位者**：interrupt handler，运行在 JS 线程。文档没有单独一句“只由 interrupt handler 置位”，但 L196-198 的上下文是明确的。**判：成立，表述可补一句。**
2. **内存序**：因为写与读都在 JS 线程（interrupt handler 写，`pump_jobs` 在 `JS_ExecutePendingJob` 返回后读），同一线程的 program order 足够，**不需要 `_Atomic` 或锁**。文档没有写内存序；只要不引入外部 watchdog 线程，就没有跨线程问题。**判：当前方案不需要原子；若将来改成外部线程置位，必须补 `_Atomic` 或生命周期锁。P2 表述。**
3. **interrupt handler 读到的值是否最新**：同线程写、同线程读，读到的一定是最新值。QuickJS 不提供跨线程屏障，但这里没有跨线程。**判：成立。**
4. **700 ms 由谁计时**：由 interrupt handler 内的 `now_ms()` 计时，运行在 JS 线程。它不会“同样卡死”，因为它本身就是被 QuickJS 周期性调用的回调。前提是 `now_ms` 无阻塞——design L403 已要求“单调、无阻塞、无分配、可从 JS 线程调用”。**判：成立。**
5. **drain 线程是否参与计时**：不参与。drain 线程睡在 `thread_join`，没有 watchdog。因此对“C 侧 handler 死等”确实无解；文档 L201 已诚实写明“硬上限无法打断，shutdown 会等 join”。**判：与文档一致，不是缺陷，是已声明的边界。**

补充一条你没问但相关：**如果实现者为了“更稳”加一个外部 watchdog 线程，就必须把 `close_abort` 改成 `_Atomic`，并解决 `now_ms` 的跨线程调用问题。** 文档应明确“不采用外部 watchdog”，否则这条修法在实现层会被做歪。P2。

**P0 结论：清零。**

---

## 二、C 侧 handler 死等

6. **可检测手段**：目前只有一个统计键 `NX_STAT_CORE_BLOCK`（design L491），语义是“debug 下 Core handler 超时未返回的次数”。但**没有任何组件能产生它**：没有独立定时器线程，JS 线程正卡在 handler 里，drain 线程不知道 handler 何时进入。除非 handler 返回后由包装层比较“进入时间戳”和“返回时间戳”——那统计的是“超时后才返回”，不是“未返回”。所以这个键当前不可实现。**P2 方案表述（观测项名不副实）。**
7. **宿主契约写在哪**：async L201 与 §4.3 写了“Core 不得阻塞”；design 的 Core 职责表也有。但 Swift/Kotlin 侧没有任何可见提示：生成绑定注释、生成器告警、运行时检查都没有。**P2。**
8. **DEBUG 断言**：没有。建议在 handler 包装层加 `#ifdef NEXCORE_DEBUG` 的计时断言/日志（进入时间 + handler 名），并让 `NX_STAT_CORE_BLOCK` 只在“返回后超阈值”时自增，名字改成 `NX_STAT_CORE_SLOW`，避免声称能检测“未返回”。**P2。**

---

## 三、三个缺口

9. **JS 崩溃的 abnormal 分支**：差异是——`js_done==false`、跳过 `JS_Free*`、记 `NX_STAT_JS_THREAD_ABORTED`、`shutdown` 返回 `NX_E_INTERNAL`、DEAD 仍由 `finish_close()` 置。**会不会掩盖原因：会。** 分支只记一个统计键，没有带出最后的 JS 异常 message、`NX_STAT_LAST_ERROR` 或退出原因枚举。L258 说“运行时视为已丢失，由 ASAN 报告”也不准确：ASAN 不会报告“线程提前退出”，只有 LSAN 可能报泄漏；如果线程是崩溃退出的，进程通常直接死，走不到 join。**P2 表述。** 建议 abnormal 增加 `reason`（early-return / pthread-fail / fatal-unwind）与 debug 下的最后异常 message。
10. **FATAL 自行释放 runtime**：**只有在 `JS_ExecutePendingJob` 返回、JS 栈展开之后释放才安全。** 如果 FATAL 由 interrupt handler 触发，handler 返回非 0 → QuickJS 抛中断异常 → job 返回 `r<0` → pump 返回 `E_JS_RUNAWAY` → turn 边界进入 FATAL，此时 C 栈已回到 pump，释放安全。但文档 L280/L511 只写“JS 线程自行释放 runtime”，没有写“必须在 job 返回后、turn 边界释放；禁止在 interrupt handler 内释放”。按字面实现有 UAF 风险。**P1 表述（方案本身成立，缺一句顺序约束）。**
11. **JS 线程内 `free` 返回 `NX_E_REENTRANT` 后怎么结束自己**：文档给了间接答案——JS 线程内只能发起 `shutdown`（只置 stop、立即返回 0、不设 DEAD），关闭 turn 在当前 JS 栈返回后继续，JS 线程随后自然退出；`free` 必须由非 JS 线程在 `drain_active==0` 后调用（async L256-257、design L451/L516）。**正确用法存在，但没有一处把它写成“JS 线程内应做什么”的完整句子**，生成绑定也没有提示。**P2 表述。** 建议加一句：“JS 线程内只调用 `shutdown`，然后立即 return；不要调用 `free`；外部线程在 drain 退出后 `free`。”

---

## 四、b′ 与场景表

12. **“map 误当实现单元：编译可能过、链接必失败”不精确。**

- **编译会失败的情况**：消费者没有 `import <ns>::<ns>_map`，直接用裸名/限定名 —— CT 前端就拒绝（GAP-14），走不到 `cc`；或者 map 的 prologue 头文件缺失 —— `cc` 编译失败。
- **能编译通过的情况**：消费者 `import` 了 map，调用 `<ns>::<Trait>.<method>`；map 的 prologue 已带入目标头，只有原型，没有定义。
- **链接会不会失败**：**只有在目标实现单元没有进链接清单时才失败**（典型是构建清单误写成了 `a_map.m.ct` 而不是 `a.ct`）。如果目标实现 `a.c` 正常参与链接，那么“误 import map 而不是实现”**不会失败**，因为 map 只是声明与 recipe，不碍事。

所以准确说法是：“误把 map 当实现单元**加入构建**，链接必失败；误 import map 而实现仍在构建里，不会失败。”现在的措辞把“import”与“加入构建”混在一起，防护规则也就说不清。**P2 表述。**

13. **三处同步的 AST 级断言可实现。** 数据来源是生成器的全局符号表（SYM-01 规定先扫描全部单元）：目标导出头给出 `(ns, trait, method, cname, 签名)`，map 的 recipe 由同一符号表生成，调用点在同一棵 AST 里。生成器同时持有三份信息，断言“三处一致、无重复 key、无残留 cname”是可行的。**前提是生成器必须拿到完整单元集合**；文档没有写“生成器必须在全量单元上运行，否则符号表不完整”。**P2 补一句。**

14. **多 trait 同名、签名相同的“静默合并”不是 bug。** recipe key 是 `(ns, trait, method)`，落到同一个 cname 且签名相同，说明它们指向同一个 C 符号——语义本来就相同，合并是别名，不是覆盖。真正要报错的是“同一 cname、签名不同”，文档已经这么写。建议在 manifest 里把“共享 cname 的多个 key”标成 `alias`，让评审看见这是有意的。**P2。**

15. **五场景表逐条：**

| 场景 | 判断 |
|---|---|
| 同步死循环 | 与微任务洪水**最终码相同**（`E_JS_RUNAWAY` + FATAL），区分只在统计键（`JS_RUNAWAY` vs `PUMP_BUDGET`）与触发路径。**对调用方无差别，对诊断有意义**；文档只声称统计区分，没有夸大，成立 |
| 微任务洪水 | 同上 |
| 无限 await | `E_TIMEOUT`，不置 abort，正确 |
| C 侧同步阻塞 | 无错误码、join 等待；文档已承认不可抢占，但**验收明确不构造**，所以这条边界**没有通过/失败判据**。加上 `NX_STAT_CORE_BLOCK` 不可产生，等于“既没测、也测不了”。**P2** |
| 关闭 turn 内死循环 | `close_abort` → `NX_E_CLOSE_TIMEOUT`，不 FATAL，正确；与 ACC-C-11 一致 |

**额外发现（P1）：`pump_jobs` 的契约级伪代码在关闭 turn 下仍会 `set_abort()`。** L169-174 是无条件 `set_abort(); return E_JS_RUNAWAY`，而 §7.1 L250 要求“优雅预算耗尽只记统计、不再开始新 job，不置 abort、不 FATAL”。文档说“复用同一 pump，但使用独立的优雅预算”，却没有在伪代码里给出 `closing` 分支。按 §0 “伪代码是契约级示意”，这两处是**契约级冲突**：实现者照伪代码写，就会在 500 ms 时把关闭打成 FATAL。**P1 表述（方案意图清楚，伪代码需补一支）。**

---

## 五、验收与残留

16. **ACC-C-11 的 200 现在有两个含义。** 硬上限 = `shutdown_budget_ms + 200` = 700（200 是“硬上限增量”）；验收写“700 ms + 200 ms 调度余量”（200 是“测试容忍”）。两个 200 不同义，却都叫 200。原来的“500+200”语义没丢（被吸收进 700），但新增的 200 没有定义：从哪个时刻量到哪个时刻、用什么时钟、CI 抖动怎么算。**P2 表述。** 建议起三个名字：`shutdown_grace_ms=500`、`shutdown_hard_extra_ms=200`（硬上限=700）、`shutdown_test_slack_ms`（测试阈值），并写清计时边界（`shutdown` 进入到返回）。
17. **上轮 P1 15 条 / P2 10 条的状态**见下表。
18. **`MIN_SIZE`/`CURRENT_SIZE` 双向兼容：判据基本写清，但缺两个测试。**

- 新宿主 → 旧库：`struct_size > 旧库已知布局`，旧库按 `>= MIN` 接受、只读已知字段、忽略尾部。design L394 写清。
- 旧宿主 → 新库：`struct_size == MIN`，新库对 `MIN..CURRENT` 之间缺失字段取默认值。design L394/D-18 L371 写清。
- 缺 1：**没有规定“库读取任何字段前必须先判 `offset+size <= struct_size`”**。ACC-A-04 只测了“大 struct_size + 尾部垃圾”（忽略尾部），没测“短 struct_size”下不发生越界读。应加：`struct_size == MIN` + ASAN，证明不读缺失字段。
- 缺 2：**`abi_version` / `protocol_version` 不匹配返回什么码没写。** 错误码表只有 `NX_E_SCHEMA_MISMATCH`（schema hash），版本不匹配没有归属。**P2。**
- 另：decisions L208 仍写“追加可选字段只更新 `NEXCORE_CONFIG_V1_SIZE`”，与 D-18 L372/design L395“只更新 `CURRENT_SIZE`、`MIN_SIZE` 冻结”直接冲突。**P2 表述，且是明确的自相矛盾。**

---

## 六、上轮 P1/P2 逐条状态

### P1（上轮 15 条）

| 上轮 | 内容 | 状态 |
|---|---|---|
| 1 | 3a JS 崩溃路径 | **已改**（3a-a abnormal） |
| 2 | FATAL 绕过单点 DEAD | **已改**（FATAL 经 `finish_close()`，L504/L511/L280） |
| 3 | JS 线程内 `free` | **已改**（`NX_E_REENTRANT`，L277/L451） |
| 4 | `E_JS_RUNAWAY` 来源/误分类 | **已改**（`r<0` 按 `close_abort`→`abort_requested`→异常分类；场景表） |
| 5 | D-15 四维不公平 | **已改**（只保留可校验性 + AI 友好度，并列出“不构成差异”的维度） |
| 6 | b′ 代价未写全 | **已改**（三处同步、多 trait、map 误用、stale map、头名冲突） |
| 7 | transport 最小接口未定义 | **未改**：仍只有 design §1.3 一句“transport 契约”，实际接口在 roadmap P4 |
| 8 | “不得实现业务语义”无检查 | **已改**（design L124 明确为人工规则；ACC-E-04 同步） |
| 9 | §11 `platform/` 归属冲突 | **已改**（L570-571 分开框架仓库/宿主仓库） |
| 10 | protocol 关闭预算语义 | **已改**（L338 写明超硬上限中断、剩余 reaction 不执行） |
| 11 | `DROPPED_SHUTDOWN` 计数 | **已改**（三类各 +1，L267-271，`==300` 可推） |
| 12 | ACC-C-11 的 500+200 | **部分改**：改成 700+200，但 200 的语义重叠（见 §五-16） |
| 13 | bench-v1 不可比 | **已改**（设备/频率/重复 3 轮+IQR/不剔除离群/种子+SHA-256/p50-p95 统一插值/handler 分段/预算） |
| 14 | config 单向兼容 | **已改**（MIN/CURRENT、双向、默认值规则） |
| 15 | Swift/Kotlin 时钟注入 | **已改**（design L400-404 `@convention(c)`、JNI 蹦床、单调无阻塞无分配） |

**剩余：P1-7（transport）未改；P1-12（200 语义）部分。** 新增 P1：关闭 pump 伪代码与 §7.1 冲突；FATAL 释放时机缺一句。

### P2（上轮 10 条）

全部已改：README 更新到 `SYM-01..SYM-05`、v0.6 变更记录、ACC-C-03 假时钟、ACC-A-04 负例措辞、ACC-A-11 重试 create、ACC-A-08 barrier、p50/p95 插值、ACC-E-08 fixture、C-09 UB 说明、map 误用/stale/头名。

**上轮 P2 剩余 0 条。**

---

## 七、本轮问题清单

### P0

**无。**

### P1（4）

1. **（方案缺陷）transport 最小接口仍未定义**：design §1.3 把它算作框架交付物，正文没有签名/状态机/错误处理；roadmap L182 推到 P4。平台接入无法按文档开工。
2. **（表述）`pump_jobs` 契约级伪代码在 closing 下仍 `set_abort()`**：与 §7.1 L250“优雅预算耗尽不置 abort、不 FATAL”冲突。需补 `closing` 分支：预算耗尽只记 `CLOSE_BUDGET_EXCEEDED` 并停止启动新 job。
3. **（表述）FATAL 释放 runtime 的时机未写死**：必须“在 `JS_ExecutePendingJob` 返回、JS 栈展开后于 turn 边界释放”，禁止在 interrupt handler 内释放。否则按字面实现有 UAF。
4. **（表述）ACC-C-11 的 200 语义重叠**：一个 200 是硬上限增量，一个是测试调度余量；需拆成三个具名常量并定义计时边界。

### P2（9）

1. `close_abort` 未明写“只由 JS 线程的 interrupt handler 读写；不采用外部 watchdog；若采用必须 `_Atomic`”。
2. `NX_STAT_CORE_BLOCK` 无法产生（无 watchdog），且验收明确不构造该场景；建议改名 `CORE_SLOW` 或在 DEBUG 包装层用“返回后计时”实现。
3. 宿主契约“Core 不得阻塞”没有在 Swift/Kotlin 绑定或生成物中体现；DEBUG 下无断言/日志（handler 名 + 进入时间）。
4. abnormal 分支掩盖原因：无最后异常 message / 原因枚举；“由 ASAN 报告”说法不准确（应区分 LSAN 泄漏与进程崩溃）。
5. JS 线程内结束自己的完整用法未成句：“只调 `shutdown` 并 return，`free` 由外部线程在 drain 退出后调用”。
6. “map 误当实现单元”措辞把“import”与“加入构建”混为一谈；链接失败只在后者发生。
7. 生成器需声明“必须在全量单元上运行”，否则三处 AST 断言不完整。
8. 多 trait 同签名共享 cname 应在 manifest 中标为 alias（当前只是允许）。
9. D-18 L208 仍写“只更新 `V1_SIZE`”，与 L372/design L395 冲突；另缺 `struct_size == MIN` 的 ASAN 测试与 `abi_version`/`protocol_version` 不匹配的返回码。

---

## 八、结论

**P0 清零，可以进入编码。**

理由：上一轮唯一的 P0（关闭 turn 单 job 无限挂起）已经由 interrupt handler 硬抢占收口，且这条修法在技术上成立——置位者是 QuickJS 在 JS 字节码执行期间回调的 interrupt handler，不是“卡住后才轮到的主流程”；置位、读标志、计时都在 JS 线程内，不需要跨线程原子；C 侧阻塞被明确列为宿主契约违规，不再假装可抢占。

**可以进入编码**：关闭流程、b′ 拓扑与生成器、协议/ABI/事件/验收 A·B·C·D·E·H 组、config 双向兼容。
**编码前建议顺手修掉**（不阻塞，但会反复被问到）：P1-2 的 pump 伪代码补 `closing` 分支、P1-3 的 FATAL 释放时机一句话、P1-1 的 transport 接口（影响平台接入排期，不影响核心库）、P1-4 的 200 命名。
