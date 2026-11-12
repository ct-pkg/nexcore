# 设计文档第三轮复看

- 复核对象：`docs/` v0.5（README 83、acceptance 208、roadmap 229、ai-friendly 289、open-questions 299、protocol 356、decisions 365、async 375、design 541、codegen 643）
- 复核方式：只读 `docs/`；实验沿用 `/tmp/ctmap`、`/tmp/ctm6`；本报告只写 `reviews/`
- 结论：**不通过**。P0 **1** 条、P1 15 条、P2 10 条
- 与上一轮相比：b′ 真落地了，57 条验收没有残余业务断言，编号范围正确；关闭流程的 3b 修复引入了一个新的、可复现的挂死路径

分级：

| 级别 | 方案缺陷 | 文档表述问题 |
|---|---|---|
| P0 | 1（关闭 turn 单 job 无上限） | 0 |
| P1 | 5（3a 崩溃路径、`free` 自 join、transport 接缝、b′ 代价、config/时钟） | 10 |
| P2 | 0 | 10 |

---

## 一、b′ 是否真的落地

### 1. 根转发残留：**没有两套，通过**

全库搜索 `转发`/`forward`，命中只有三处性质：

- design §3.4 L120/L137/L151-168：选择说明与被否决理由；
- decisions D-15 L289-313：备选表、决定、后果；
- README v0.5 变更记录 L83。

没有 `nexcore_forward`、没有转发接口签名、没有注入时机/存储位置/初始化顺序的旧设计正文。GEN-17、SYM-04、ACC-E-08 的正文都已是 map 拓扑，旧正例只作为“已作废”的说明存在（acceptance L30）。

**判定：通过，没有两套方案并存。**

### 2. GEN-17 / GEN-20 / SYM-04 / SYM-05 / ACC-E-08：**内容实质存在，通过（E-08 留一个 P2）**

不是“把禁止改成映射”的空改写：

- GEN-17（codegen L517）：给出产物 `<ns>_map.m.ct`、消费者 `import <ns>::<ns>_map`、调用形态 `<ns>::<Trait>.<method>`、禁止项、负例要求，且挂到 SYM-05 五条检查。
- GEN-20（L520）：给出 map 单元的四要素（prologue include、静态 trait、cname recipe、无实现），并要求 recipe callee == 符号表 cname、map 不 import 实现单元。这是可核的产物约束。
- SYM-04（L545）：明确“只允许经映射单元”，并禁止裸名、手写 cname、根转发三种写法。
- SYM-05（L546）：五条检查逐条列出。
- ACC-E-08（acceptance L120）：正例（A↔B 双向、import map、`<ns>::<Trait>.<method>`）、负例（无映射的裸名/限定名/cname 三种）、跨单元 `&f` 失败 + wrapper 通过、SYM-05 五条、生成 C 无 `::`。

**ACC-E-08 现在可判定**：正例看编译/链接/运行退出码；负例看 `cc` 是否失败；`&f` 看编译失败、wrapper 看通过；五条检查看脚本输出。全部外部可观测。

P2：E-08 没有把 A、B 的 fixture 文件内容固定下来（`tests/topology/` 之类）。不同人构造的“正例”可能写法不同，建议固定两个最小单元文件，和 `/tmp/ctmap` 一致。

### 3. 五条机械检查能否机械执行

| # | 检查 | 能否脚本 | 需要什么 | 判断 |
|---|---|---|---|---|
| ① | `ct build` 退出 0 | 能 | 单命令 | 可脚本。但 CT 不校验跨单元符号（GAP-14），退出 0 **不是正确性证据**，只能当必要条件 |
| ② | 每个 `.c` 过 `cc -Werror=implicit-function-declaration` | 能 | 编译器 + include 路径 | 可脚本，且是真正的门禁 |
| ③ | 生成 C 无 `::` | 能 | 去注释/字符串后的 `grep` | 裸 `grep -nE '::'` 会命中字符串字面量和注释；不需要完整 AST，C lexer 或“先剥注释与字符串再 grep”足够 |
| ④ | recipe callee == 符号表 cname | **不能独立脚本** | recipe 语法解析 + 符号表 | 纯正则不够（参数、换行、别名、多个 recipe）。生成器本来有 AST，应做成生成器内部断言 + golden，不冒充外部脚本 |
| ⑤ | 命名空间单元只 import `.m.ct` | 能 | 行级 grep | 可脚本，前提是 import 单行且格式固定，并排除根单元 `NexCore.h.ct`（ai-friendly 检查 10 已排除）。多行 import / 别名未定义，建议在 SYM 里固定语法 |

**结论**：①②③⑤ 可写成 CI 脚本；④ 必须留在生成器内，不应列为“独立机械检查”。五条里有一条名不副实。

### 4. b′ 的代价是否写全：**没写全，P1**

design §3.4 L158-163 写了四条代价：多一个生成文件、维护 trait/recipe、`&ns::fn` 不可用、生成物要评审。缺的：

1. **接口变更要同步三处**：目标单元签名一变，`<ns>_map.m.ct` 的 recipe 与调用方都必须同步；文档只写“维护 trait/recipe”，没有写“三处同步 + stale 检测”。GEN/SYM 也没有“接口变更必须同时更新 map”的规则。
2. **多个 trait / 多个 map 的命名冲突未定义**：`<ns>::<Trait>` 的 Trait 名由谁定、同命名空间拆多个 trait 时怎么命名、一个消费者 import 多个 map 时 trait 同名怎么办，全部空白。GEN-20 只要求“静态 trait”。
3. **map 被误当实现单元 import 的防护只靠后缀**：检查 ⑤ 只查调用方的 `import` 行；没有“map 单元不得出现在实现单元的链接清单/导出符号”的反向检查。
4. **stale map 文件清理未定义**：删除一个命名空间后，旧的 `*_map.m.ct` 是否必须删除；`--check` 只比对生成物内容，不检测多余文件。
5. **生成头名冲突未验证**：map 的 prologue include 是 `<ns>.h`；平铺输出目录下 `a` 与 `a::b` 的头文件名是否冲突，文档没有规则，标**未验证**。

### 5. D-15 的四维比较：**对 b 不完全公平，P1（文档表述）**

- **耦合度**：b 新增一次 A→B 交互，要改 A 调用点 + 根转发注册（两处）；b′ 要改 A 调用点 + B 的 map recipe（也是两处）。文档用“b 必须同时改两个单元”暗示 b 更差，但**两处改动的数量相同**，差别只是“改在中心还是改在目标本地”。这一点不公平。
- **调试**：b 的转发如果生成成 `static inline`，栈层可以消失；文档没有验证就断言“多一层”。属于未证实的优势。
- **可校验性**：b 的转发与注入同样可以由符号表声明式生成；“过程式”是**设计选择**，不是方案固有属性。把 b 写成“必然难机械校验”不成立。
- **AI 友好**：b′ 的回调仍要调用方写 `static` wrapper，调用形态并不完全统一；“统一调用形态”被高估。

**判定**：b′ 仍然可选、也已实测成立；但“四项均更优”的绝对表述不成立。应改成“在点对点依赖与声明式产物上更优；在改动处数、回调形态上与 b 持平”。

---

## 二、关闭流程

### 6. 3a：DEAD 单点覆盖情况

| 路径 | 是否覆盖 | 依据 | 缺口 |
|---|---|---|---|
| 正常 shutdown（非 JS 线程） | 覆盖 | async L241 join → `finish_close()` | — |
| JS 线程内发起 | 覆盖 | L242 只置 stop、返回 0、不设 DEAD | — |
| 未 shutdown 直接 free | 覆盖 | L260 `stop_requested==true` 否则 `NX_E_BUSY` | — |
| shutdown 超预算 | 覆盖 | L236 记统计继续 → L239 `js_done` → join → `finish_close()` | — |
| shutdown 期间 JS 崩溃 | **未覆盖** | — | 若 JS 线程异常退出但没走到 L239，`thread_join` 返回 `joined==true` 而 `js_done==false`；`finish_close()` 不置 DEAD，`shutdown` 仍返回 0，句柄卡在 STOPPING；随后 `free` 断言 DEAD 失败。文档没有“join 返回后 `js_done` 必须为真，否则按内部故障处理”的规则 |
| FATAL 清理 | **半覆盖** | design L472 状态图 `FATAL ──cleanup──> DEAD` | 图上的 cleanup 没有写是否经 `finish_close()`；与 L244“DEAD 单点”直接冲突。若实现按图走，就绕过 join |
| `free` 在 JS 线程内调用 | **未覆盖** | — | 文档只禁止 JS 线程内 shutdown 自 join，没有禁止 `free`；`free` 在 `joined==false` 时会 `thread_join(self)` |

**“单点设置”本身没有第二条 SET 语句**，但有两条绕过/卡死路径：FATAL cleanup 的图、`js_done` 缺失时的静默卡住。P1。

### 7. 3b：`E_JS_RUNAWAY` 是否唯一，四种失控走哪

| 场景 | 走哪个标志 | 最终码（按伪代码） | 问题 |
|---|---|---|---|
| 长时间 `while(true){}`（handler / eval） | interrupt 50 ms → `abort_requested` | `JS_ExecutePendingJob` 返回 `<0` → L178-180 走 `E_JS_EXCEPTION` | 与 L190/L193“触发时返回 `E_JS_RUNAWAY`”矛盾。interrupt 引发的 abort 被分类成 JS 异常 |
| 无限 `await`（Promise 永不 settle） | 不置 abort | C pending 超时 → `E_TIMEOUT` | 正确 |
| 微任务洪水 | pump 的 `hard_deadline` → `abort_requested` | L174 `E_JS_RUNAWAY` | 这是伪代码里**唯一**真正产生 `E_JS_RUNAWAY` 的路径 |
| handler 内同步阻塞 | 同死循环 → interrupt | `E_JS_EXCEPTION` | 同第一条 |

关于“唯一来源”：

- 置 `abort_requested` 有两处：L173（pump 预算）与 L195（interrupt handler）；L173 自己写的是“位置**之一**”，L193 却写“唯一来源”。措辞自相矛盾。
- 若“来源”指错误码，则 `E_JS_RUNAWAY` 只有 pump 一处，interrupt 路径反而落成 `E_JS_EXCEPTION`；那“唯一”成立但语义错位。

**P1：`E_JS_RUNAWAY` 的来源在伪代码层不成立。**

**关闭 turn 豁免 interrupt 会让失控脚本无限拖住——P0。** 理由：

- `pump_jobs` 的 `hard_deadline` 检查在循环顶部，**只在两个 job 之间**执行；
- 单个 job 若是 `while(true){}`（例如某个 `.catch` 里同步死循环），执行不会返回 `JS_ExecutePendingJob`，循环顶部永远到不了；
- 关闭期间 `closing==true` 又豁免 interrupt，于是没有任何机制能打断这个 job；
- 非 JS 线程的 `shutdown` 卡在 `thread_join`，**永不返回**；
- 这与 ACC-C-11“`shutdown` 在 500 ms + 200 ms 内返回”和 RC-16“断言不出现 `E_JS_RUNAWAY`”直接冲突。RC-16 的测试用“长 `.catch`”构造，如果长的是同步死循环，测试自己会挂死。

`shutdown_budget_ms` 只约束“job 之间”，不约束“单个 job”。这是方案缺陷，不是措辞问题。

### 8. RC-12 / RC-16 与 §5.2、§7.1 是否一致

- RC-12（L301）与 §5.2 L186-190 一致：10k 是让出配额，200 ms / 50 ms 触发 abort → fatal。
- RC-16（L305）与 §5.2 L196、§7.1 L232 一致：`closing==true` 豁免 interrupt。
- 三处（§5.2、§7.1、RC-16）**描述一致，但一致地漏了“单 job 不可抢占”**；不是出现三套顺序。
- RC-16 与 ACC-C-11 的时限断言互相冲突（见上）。

---

## 三、定位

### 9. 架构图归属与接缝

归属本身改对了：design §3.1 表明确只有 Bridge 属框架，UI/Core/JS 属宿主；§2 上下文图、§3.2 依赖矩阵、§3.3 八条禁止项都与之一致。

接缝的位置是：`nexcore_*` ABI + 生成绑定 + drain 线程契约。但：

1. **transport 的“最小接口”没有定义**。design §1.3 L34 把它列为框架交付物，全文没有接口签名、状态机或错误处理约定；roadmap L182 说“transport 接口文档 + 最小骨架”是 P3 产出。也就是说，接缝**被推迟到 P3**，当前不可执行。
2. **“框架不得实现业务语义”没有可执行边界**。§3.3 L116 是规则，但没有对应脚本；`check-boundaries.sh` 只能查平台头、`nexcore_` 导出、`#ifdef`、errno、`&ns::`、trait、`__nx_*`，查不了“业务语义”。ABI 7 符号白名单与“`pub nexcore_` 只在根”是间接约束，不能证明 Bridge 里没有计价/状态机。
3. **§11 L538 把 `platform/` 列进编码目录**，与 ai-friendly L44“`platform/` 在宿主项目仓库”冲突。

**判定**：接缝归属清楚，但接缝契约未定义（P1 方案缺口）；业务语义规则应显式标为“人工规则，不冒充自动门禁”（P1 文档表述）；§11 的目录冲突是 P1 文档表述。

### 10. 57 条里有没有残余业务验收：**没有，通过**

逐组看：

- A（11）：ABI、构建、布局、时钟、Schema 不匹配——纯框架。
- B（8）：信封、错误码、id、超时、取消、通知、订阅、事件顺序——纯协议。
- C（10）：线程、MPSC、微任务、非阻塞、关闭、失控、重入、条件变量、循环、预算——纯线程。
- D（4）：订阅、保序、背压、异常隔离——纯事件。
- E（8）：确定性、漂移、负例、无魔法、四端、wrapper、trait、拓扑——纯生成。
- F（5）：边界脚本、扩展流程、API 完整性、越界拦截、门禁——纯边界。
- G（6）：G-01/G-02 已改为框架断言（transport/sink/超时/取消/关闭/schema_hash/往返计数），G-03/04/05/06 是 hash、soak、性能、生命周期——纯框架。
- H（5）：限额、无动态执行、白名单、脱敏、资源上限——纯安全。

原来依赖业务结果的 G-01、G-02、F-02、E-05 都改成了 fixture 或 stub。HOST-01..04 单独列在 §14。**51 条原本就是框架能力断言，没有“某个业务结果正确”的要求。**

唯一条件性问题是 ACC-C-05 的 `==300` 仍要跨 §7.1/§7.2/§6.4 三节才能推出（见 P1-3），这不是业务断言。

### 11. C-09 的澄清：**可执行，P2**

新判据（acceptance L97）：1000 次 create→shutdown→free；每组在 `shutdown` 后、`free` 前调 `send`/`drain`，断言 `NX_E_SHUTDOWN`；`free` 后不再调用（UB 不在验收范围）；ASAN/LSAN 无报告。

- 可执行：状态码与 ASAN 报告都是外部可观测。
- 是否变成“不测”：对“free 后旧句柄返回句柄错误”确实不测了；这是**正确的删除**——原判据与 design §7“free 后调用是 UB”冲突，保留反而是错的。
- P2：建议在条目里加一句“free 后 UAF 由 ASAN/调用方契约覆盖，不作为 ABI 行为断言”，避免后来的人以为漏测。

---

## 四、其他

### 12. README 编号范围

| 前缀 | README 声明 | 实际最大编号 | 是否对上 |
|---|---|---|---|
| D | D-01..D-19 | D-19（无 D-20） | 对上 |
| FIX | FIX-01..FIX-16 | design L199 FIX-16 | 对上 |
| GAP | GAP-01..GAP-14 | design L360 GAP-14 | 对上 |
| GEN | GEN-01..GEN-20 | codegen GEN-20 | 对上 |
| SYM | SYM-01..SYM-05 | codegen SYM-05 | 对上 |
| RC | RC-01..RC-16 | async RC-16 | 对上 |

**但** README L66 仍写“`R-01..R-03` 已被 `SYM-01..SYM-04` 取代”，范围没更新到 SYM-05。P2。

### 13. v0.5 变更记录：**漏记，P2**

L83 只列了 D-15 b′、`#ct-raw`、3a/3b。漏记：

- GEN-20 与 SYM-05 新增；
- ACC-E-08 改写、ACC-C-09 澄清、ACC-G-01/G-02/F-02/E-05 改写；
- acceptance §10 性能协议补充（Release、并发度 1、10k/100k、分段计时）；
- D-18 的 `now_ms`/`clock_user` 与 `struct_size` 语义；
- FIX-16/GAP-13/GAP-14 的重述；
- README 编号范围更新（本身也没做，见 12）。

### 14. 上一轮 P1 8 条 / P2 5 条的状态

| 上轮编号 | 内容 | 状态 |
|---|---|---|
| P1-1 | `#ct-raw` 标注不准确 | **已改**：design L169、decisions L300/L302 改为“已实测可用但主动不采用” |
| P1-2 | 关闭预算的调用方语义 | **未改**：protocol §12.6 L337 仍无条件写“随后泵空，`.catch` 有机会执行”，没提预算耗尽例外 |
| P1-3 | `DROPPED_SHUTDOWN` 计数口径 | **未改**：§7.1 表 response 行仍无计数、inbox 行仍是无名“累加计数” |
| P1-4 | ACC-C-11 的“500 + 200” | **未改**：acceptance L99 原样 |
| P1-5 | bench-v1 环境与离群值 | **部分改**：新增 Release、1 KiB、并发度 1、10k/100k、p99 线性插值、含/不含 handler 分段、Android 关省电；仍缺设备型号、离群值策略、重复次数/方差、macOS 频率、稳态判据、payload 种子与哈希、吞吐采样、 “不含 handler” 的构造、p50/p95 插值 |
| P1-6 | config 单向兼容 | **未改**：D-18 L355 仍写“追加字段只更新 V1_SIZE”，没有 MIN/CURRENT，也没有区分 forward/backward |
| P1-7 | Swift/Kotlin 时钟注入 | **未改**：design 仍只有 C 结构体字段；无绑定表达、无线程安全/单调性要求 |
| P1-8 | `js_done` 判断时机 | **部分改**：3a 的 join 规则已修正常路径；但新增“`js_done` 缺失”“FATAL 绕过”“JS 线程内 free”三个缺口（见 P1-1..P1-3 本轮） |
| P2-1 | ACC-C-03 与 200 ms | 未改 |
| P2-2 | ACC-A-04 措辞 | 未改（acceptance L63 原样） |
| P2-3 | ACC-A-11 可观测 | 未改（L70 原样） |
| P2-4 | ACC-A-08 barrier | 未改（L67 原样） |
| P2-5 | p50/p95 插值 | 未改（L160 只定义 p99） |

---

## 五、本轮问题清单

### P0（1）

**P0-1（方案缺陷）：关闭 turn 豁免 interrupt 后，单个长 job 无法被 `shutdown_budget_ms` 抢占，`shutdown` 会永久挂起。**

- 位置：async §5.2 L169-183（deadline 只在循环顶部）、L196（closing 豁免）；§7.1 L232/L236；RC-16 L305；acceptance ACC-C-11 L99。
- 触发：某个 `.catch`/`.finally` 是同步死循环或超长同步计算；关闭 turn 泵到它时，`JS_ExecutePendingJob` 不返回，interrupt 又因 `closing==true` 不返回非 0。
- 后果：非 JS 线程 `shutdown` 卡在 `thread_join`，永不返回；ACC-C-11 的 500+200 ms 断言无法满足；RC-16 的测试本身挂死。
- 修法方向（择一）：
  - 关闭 turn **不豁免** interrupt，而是把“preempt 后进入的关闭状态”与“失控 FATAL”分开：interrupt 在 closing 期间可以被调用一次，超预算后返回非 0，但结果记为 `CLOSE_BUDGET_EXCEEDED` 而不是 runaway；
  - 或给关闭 turn 增加“单 job 看门狗”（JS 线程外的 watchdog 线程在预算到期后设置 `abort_requested`，interrupt 在 closing 期间也读它）；
  - 无论哪种，必须明确“预算到期 → 中断当前 job → 继续关闭 → 不二次 settle”。

### P1（15，按主题合并）

**关闭流程（3）**

1. **P1（方案缺陷）3a 的 JS 崩溃/异常退出路径未覆盖**：`thread_join` 返回后若 `js_done==false`，文档没有处理规则；`shutdown` 会返回 0 但停在 STOPPING，`free` 断言 DEAD 失败。需加“join 后 `js_done` 必须为真，否则按内部故障进入 FATAL/返回错误”。
2. **P1（文档表述）FATAL 清理绕过单点 DEAD**：design L472 的 `FATAL ──cleanup──> DEAD` 与 async L244 冲突。要么删掉这条边的 DEAD 标注，要么写明 cleanup 也必须调 `finish_close()`。
3. **P1（方案缺陷）`free` 在 JS 线程内调用未禁止**：会 `thread_join(self)`。需在 async §7.1 与 design §7 明确“`free` 只能在非 JS 线程调用”，并给出错误码（建议 `NX_E_REENTRANT`）。

**3b（1）**

4. **P1（方案缺陷 + 文档表述）`E_JS_RUNAWAY` 来源不唯一、interrupt 路径误分类**：L173“之一” vs L193“唯一”；interrupt 触发的 abort 在伪代码里走 `r<0 → E_JS_EXCEPTION`。需在 `r<0` 分支先查 `abort_requested`，或在 interrupt 路径显式返回 runaway。

**b′（2）**

5. **P1（文档表述）D-15 四维“均更优”不公平**：“改两处”对 b 与 b′ 相同；“转发多一层栈”未验证；“b 必然过程式”不成立；“调用形态统一”有回调例外。改为分维度陈述优劣。
6. **P1（文档表述）b′ 代价未写全**：接口变更三处同步、多 trait/多 map 命名冲突、map 误用的反向检查、stale map 清理、头名冲突（未验证）。

**定位（3）**

7. **P1（方案缺口）transport 最小接口未定义**：design §1.3 声称交付，全文无签名/状态机；roadmap 推到 P3。接缝当前不可执行。
8. **P1（文档表述）“框架不得实现业务语义”无检查**：应显式标为人工评审规则（参照 GEN-10/GEN-14），不要让它看起来像 `check-boundaries` 已覆盖。
9. **P1（文档表述）§11 L538 `platform/` 归属冲突**：与 ai-friendly L44“platform 在宿主仓库”矛盾。

**上轮遗留（5）**

10. **P1-2（文档表述）关闭预算的调用方语义**：protocol §12.6 L337 仍无条件说 `.catch` 有机会执行；需加“预算耗尽时剩余 `.catch/.finally` 不再执行”。
11. **P1-3（文档表述）`DROPPED_SHUTDOWN` 计数口径**：§7.1 表 response 行无计数、inbox 行未命名；`==300` 仍需跨节推导。
12. **P1-4（文档表述）ACC-C-11“500 + 200”**：200 ms 来源未定义；建议改“`shutdown` 在预算后 ≤ 200 ms 返回，200 ms 为调度余量”。
13. **P1-5（方案缺陷）bench-v1 不可比**：设备、离群值、重复次数/方差、macOS 频率、稳态判据、payload 种子/哈希、吞吐采样、“不含 handler”构造、p50/p95 插值仍缺。
14. **P1-6（方案缺陷）config 单向兼容**：D-18 的“向前兼容”对旧调用方是破坏性的；需 MIN/CURRENT 或明确不承诺 backward。
15. **P1-7（方案缺陷）Swift/Kotlin 时钟注入**：只有 C 字段，无绑定表达、无线程安全/单调性要求。

### P2（10）

1. README L66 仍写 `SYM-01..SYM-04`，未更新到 SYM-05。
2. v0.5 变更记录漏记（见 §13）。
3. ACC-C-03 没有给测试预算，10k 链与 200 ms 硬窗口仍可能冲突。
4. ACC-A-04“严格相等实现必须失败”措辞未改。
5. ACC-A-11“JS 线程已回收”无可观测判据。
6. ACC-A-08 缺 barrier。
7. p50/p95 插值未定义（只定义 p99）。
8. ACC-E-08 未固定 A/B fixture 文件。
9. C-09 不再测 free 后 UAF（正确），但建议显式写明由 ASAN/契约覆盖。
10. map 误用防护只靠后缀、stale map 不检测、头名冲突未验证。

---

## 六、结论

**P0 没有清零，不能进入编码。**

分开讲：

**可以进入编码的部分**

- b′ 拓扑本身：`/tmp/ctmap` 已实测双向成立，GEN-17/GEN-20/SYM-04/SYM-05 的产物形态是具体的，可以按现稿实现生成器与两个 map 单元。
- 协议、ABI、错误码、事件、生成器 A/B/D/E/H 组的验收：与业务解耦，可以开工。
- 57 条验收没有残余业务断言，验收分层（ACC/SAMPLE/HOST）可以按现稿落地。

**还不能进入编码的部分**

- **关闭流程**：P0-1 未修，`shutdown` 存在永久挂起路径；3a 的崩溃路径、FATAL 单点、JS 线程内 `free` 也未闭环。关闭相关代码现在写下去要返工。
- **平台接入**：transport 最小接口未定义，宿主无法按文档接上；`platform/` 归属还有两套说法。
- **Swift/Kotlin 绑定**：`now_ms`/`clock_user` 没有绑定表达，Kotlin 侧不可实现，时钟类验收（ACC-A-09）无法在真机复现。
- **性能门槛**：bench-v1 换机不可比，p99 阈值仍“暂定”，不能作为发布判据。
- **决策依据**：D-15 的“四项均更优”需要改成分维度陈述，否则后续复审会继续围绕同一处争论。

一句话：**b′ 和定位这两块这轮是真的改好了；关闭流程的 3b 修复把一个“误进 FATAL”换成了“永久挂起”，这是本轮唯一但必须清零的 P0。**
