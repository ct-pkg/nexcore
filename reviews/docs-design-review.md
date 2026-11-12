# 需求方案评审（`docs/`）

| 项 | 值 |
| --- | --- |
| 评审对象 | `docs/` 全部 10 个文件（README、design、protocol、async-threading、codegen、ai-friendly、roadmap、acceptance、decisions、open-questions） |
| 评审依据 | [review-criteria.md](review-criteria.md) 的 C1–C7 与 P0/P1/P2 门槛；语言事实见 [ct-capability-audit.md](ct-capability-audit.md) |
| 核查方式 | 只读 `docs/`；CT 事实用 `/tmp/ctrepro3` 独立复跑；本轮不改 `docs/`、不改 `experiments/` |
| 评审日期 | 2026-11-06 |
| 结论 | **不通过**：5 个 P0、13 个 P1、9 个 P2 |

标注约定：

- **【方案】**＝方案本身会导致错误行为或无法实现；**【文档】**＝方案可能正确但表述/编号/引用使读者必然做错。
- P0 阻断合入；P1 里程碑前必须修；P2 进待办。

---

## 1. 总评

文档整体质量高于一般设计稿：分层矩阵、信封唯一、错误码表、竞态清单、禁令清单都是可执行的东西，不是口号。但它现在还不能作为实现依据，原因是三类问题：

1. **关闭路径三份文档写了三种顺序**（P0-1）。这不是措辞问题，按任一版本实现都会与另两份不一致。
2. **生成器规则把 CT 的一个静默错译当成了正确写法**（P0-2）。我在 `/tmp/ctrepro3` 复现：命名空间单元对导入函数写裸名，`ct build` 走到 `cc` 直接失败；写全限定则 CT 自己报错。按 `G-13`/`G-17`/`R-01` 生成的代码编译不过。
3. **验收条目 11 条判不了真假、14 条只能在补写条件后才能判**（P1-9）。其中 `C-03` 的 10k 链与泵预算上限正好相等，按文档自己的规则必然触发 `E_JS_RUNAWAY`（P1-2）。

### 1.1 P0 速览

| 编号 | 类型 | 问题 | 位置 |
| --- | --- | --- | --- |
| P0-1 | 方案 | 关闭 turn 的三步顺序在三份文档中互不相同 | design §7 / protocol §12.6 / async-threading §7.1 |
| P0-2 | 方案＋文档 | 命名空间单元跨单元调用无任何可编译写法；生成器规则正好生成不可编译的 C | design §5.6 G-13、codegen §8 G-13/G-17、§8.1 R-01 |
| P0-3 | 方案 | `$cancel` 只有 `target: uint32`，无方向字段；两个方向各自使用同一 id 空间 | protocol §3.1、§5.1、§6.3 |
| P0-4 | 方案 | `nexcore_free` 的「无 drain 在阻塞」检查与 drain 进入之间不是原子操作 | design §7、async-threading §8 RC-08 |
| P0-5 | 方案 | `fatal shutdown` 被引用但从未进入状态机定义 | async-threading §5.2、Q-10；design §7 无此状态 |

### 1.2 P1 速览

| 编号 | 类型 | 问题 | 位置 |
| --- | --- | --- | --- |
| P1-1 | 方案 | 四类在途对象里，「在途事件」「已接受未处理的请求」没有收尾定义 | async-threading §7.1、acceptance C-05 |
| P1-2 | 方案 | 10k 链 = `MAX_JOBS` 10000，`C-03` 按文档规则必失败 | acceptance C-03、async-threading §5.2、roadmap P2 |
| P1-3 | 方案 | `E_NOT_SUBSCRIBED` 与「未知 subId 忽略」互斥，该码实际不可触发 | protocol §4、§6.1 |
| P1-4 | 方案 | handler 返回值不可序列化时没有错误码 | protocol §4、§8 |
| P1-5 | 方案 | `result`+`error` 同时出现时「丢弃」，发起方只能等超时，与 `B-01` 冲突 | protocol §13、§4、acceptance B-01 |
| P1-6 | 方案 | `struct_size` 未定义判据，按 §6.3 读法为严格相等 → 永久无法前向兼容；`reserved` 无使用规则 | design §6.1–§6.3 |
| P1-7 | 方案 | `NexCoreConfig` 无时钟注入，时间类验收只能依赖墙钟 | design §6.2、acceptance G-04/G-05 |
| P1-8 | 方案 | Q-14/Q-12/Q-03/Q-13/Q-06 的临时默认会改 ABI 或全部生成物，必须前移到 P1 出口 | open-questions |
| P1-9 | 文档 | 验收 11 条不可判定、14 条条件可判定 | acceptance 全文 |
| P1-10 | 文档 | `G-xx` 在文档集内有三义，`F/D/R` 各两义 | README §编号约定 |
| P1-11 | 文档 | `G-10`/`G-14` 只能人读；`G-05`/`G-16`/`R-03` 名义有脚本但实现方式不可靠 | codegen §8 |
| P1-12 | 方案 | 验收未覆盖 `E_SCHEMA_MISMATCH` 与「多线程 drain → `NX_E_THREAD`」 | acceptance 全文 |
| P1-13 | 文档 | A-04 声称「改字段不升版本 CI 失败」，但没有任何实现机制；Swift/Kotlin 侧无布局自检 | acceptance A-04、design §6 |
| P1-14 | 方案 | C1 的分层禁令没有对应验收门禁（无 `quickjs.h` 越界检查、无 `nexcore_*` 导出检查） | design §3.3、acceptance F-01 |

---

## 2. 按 C1–C7 判定

| 判据 | 结论 | 依据与缺口 |
| --- | --- | --- |
| **C1 分层隔离** | 部分通过 | 依赖矩阵（design §3.2）与五条禁令（§3.3）写得比评审口径更细；`Generated` 只能调 ABI、UI 只能用 transport，方向正确。缺的是**门禁**：F-01 的 `check-boundaries.sh` 只查 `#if`、`errno`、`import`，没有「除 Bridge QuickJS 绑定单元外不得出现 `quickjs.h`/`JS_*`」，也没有「除 `NexCore.h.ct` 外不得导出 `nexcore_*`」。这两条是 C1 的核心，必须进 CI（P1-14） |
| **C2 协议对称** | 部分通过 | 一份信封、一份解析器、一份错误码表，且明确写了「共享信封，不共享语义」（protocol §1、§8），符合 C2 的「一个编解码器」要求。硬伤是 `$cancel`（P0-3）：两个方向的 id 各自从 1 开始，取消消息却只有一个无方向的 `uint32` |
| **C3 Promise 与微任务泵** | 不通过 | 泵入口唯一、有预算、有禁止重入，方向对。但关闭顺序三处矛盾（P0-1），且 10k 链与 `MAX_JOBS` 相等（P1-2）。这两条都直接决定「Promise 是否恰好 settle 一次」 |
| **C4 线程安全** | 部分通过 | 线程归属、有界队列、禁止 `JSValue` 跨线程、关闭顺序都有明文。缺 `free`/drain 的原子性（P0-4）、`fatal shutdown` 的状态（P0-5）、在途事件与已接受请求的收尾（P1-1） |
| **C5 可审阅** | 部分通过 | 字节重现、`--emit-map`、`-Werror`、`--line` 开关、模板白名单都有落点，R1–R9 大体能满足。但 E-04 宣称「运行 G-01..G-18、R-01..R-03」，其中至少 5 条不可机械检查或与 CT 事实相反（见 §3.5） |
| **C6 生命周期** | 部分通过 | 所有权分工（design §8）与「禁止返回栈上 trait 接口」的约束在；但关闭时「在途响应/事件属于谁、由谁释放」没有定义，等于把生命周期判据留空 |
| **C7 错误与取消** | 部分通过 | 禁用 `errno`、异常必须消费并释放、错误码表唯一、取消/超时/背压三态齐备，符合 C7。缺「不可序列化结果」的码（P1-4），且 `E_NOT_SUBSCRIBED` 与订阅语义冲突（P1-3） |

---

## 3. 八项重点核查

### 3.1 验收条目是否真可判定

**先更正一个数字**：`acceptance.md` 实际是 **51 条**，不是 47 条。分布为 A 7、B 8、C 9、D 4、E 8、F 4、G 6、H 5。评审按 51 条逐条判。

**判定口径**（三档）：

- **可判定**：有命令、有阈值、有明确的通过/失败二值。
- **条件可判定**：判定列本身是二值的，但缺枚举/缺种子/缺阈值，补上即可执行。
- **不可判定**：判定列含「正常」「符合预期」「稳定」「无界」这类无法证伪的表述，或依赖本身不可执行的检查。

**结果：26 条可判定 / 14 条条件可判定 / 11 条不可判定。**

| 编号 | 判定 | 依据与必须改写的地方 |
| --- | --- | --- |
| A-01 | 可判定 | 命令 + 退出码 + 零警告 |
| A-02 | 可判定 | 白名单比对；需把「文档列出的 7 个」固定成仓库内的 `abi-symbols.txt` |
| A-03 | 可判定 | 负例 + 头文件 grep + `_Static_assert` |
| A-04 | **不可判定** | 「修改字段不升版本 → CI 失败」没有任何机制：谁保存上一版布局签名？应改为「提交 `abi-layout.txt` 黄金文件，CI 比对；变更必须同时改版本号」 |
| A-05 | 可判定 | `swiftc` 退出码 0 |
| A-06 | 可判定 | `gradlew` 任务退出码 0 |
| A-07 | 可判定 | `readelf -d` 依赖白名单 |
| B-01 | 条件可判定 | 「30 正例 + 30 负例」只给了 8 类例子，未枚举 30 条；且负例中 `result+error` 的期望是「丢弃」而非「返回错误码」，与判定列冲突（见 P1-5） |
| B-02 | 可判定（附注） | 19 个码计数正确；但 `E_NOT_SUBSCRIBED` 在当前语义下不可触发（P1-3），实际只能命中 18 个 |
| B-03 | 条件可判定 | 「随机延迟/乱序」需固定种子或可注入调度器，否则每次结果不同 |
| B-04 | 可判定 | 1 ms / 50 ms 超时 + 迟到响应丢弃，二值明确 |
| B-05 | 条件可判定 | 「竞跑 1000 次」需可控时序（屏障/信号量），否则是概率测试 |
| B-06 | 可判定 | 通知无响应 |
| B-07 | **不可判定** | 「重复退订幂等/按约定报错」是二选一；且与 protocol §6.1「未知 id 忽略」、`E_NOT_SUBSCRIBED` 三方冲突（P1-3） |
| B-08 | 可判定 | 顺序/去重/容量内不丢 |
| C-01 | 可判定 | 记录线程 ID 比对 |
| C-02 | 可判定 | 已接受消息计数 + 排空后比对 |
| C-03 | **不可判定（且按文档必失败）** | 10k 长度链 = 10,000 个 job，而 §5.2 的预算上限正是 10,000，条件为 `jobs >= MAX_JOBS` 即 `E_JS_RUNAWAY`。本条与 roadmap P2「10k 链不丢」都要求成功，规则却要求失败（P1-2） |
| C-04 | **不可判定** | 只有「send 不等待 JS / drain 不等待请求完成 / UI 不被阻塞」，无任何数值上限。应改为「`send` p99 < 50 µs；`drain` p99 < 200 µs；单次调用最长阻塞 < 1 ms」 |
| C-05 | 条件可判定 | 只覆盖 JS 侧 pending。outbox 响应、outbox 事件、inbox 已接受未处理的请求都没有期望；「无回调在 free 后发生」需要指定观测点（回调里写 sentinel） |
| C-06 | 条件可判定 | 「返回 `E_JS_RUNAWAY`」未说返回给谁；「可干净重建」需定义为「新 handle `create` 返回 0 且一次 ping 成功」 |
| C-07 | 可判定 | 重入断言 |
| C-08 | 可判定 | ≤100 ms 有数值 |
| C-09 | 条件可判定 | 「句柄不复用错乱」应改为「旧句柄返回 `NX_E_HANDLE`，1000 次中 0 例外」 |
| D-01 | 可判定 | 与 B-07 内容重复，建议合并 |
| D-02 | 可判定 | 严格 FIFO |
| D-03 | 条件可判定 | 「产生一次 `E_BACKPRESSURE`」未定义「一次」的作用域：每次溢出、每次丢弃、还是每次 drain |
| D-04 | 可判定 | 双向事件 + 空载荷 + 深嵌套 |
| E-01 | 可判定 | `--check` |
| E-02 | 可判定 | 两次 diff |
| E-03 | 条件可判定 | 「20 类非法输入」未枚举，只给了 5 个例子 |
| E-04 | **不可判定** | 依赖 G-01..G-18、R-01..R-03，其中 G-10/G-14 只能人读，G-13/G-17/R-01 与 CT 事实相反（§3.5） |
| E-05 | 条件可判定 | 「每个方法各出现恰好 1 次」会被注册表字符串重复计数干扰；应改为「声明 1 处、调用点 ≥1 处」 |
| E-06 | **不可判定** | `check-callback-wrappers.py` 被描述为「所有 `&ident` 的 `ident` 必须在本单元定义」，纯正则无法区分局部变量、参数、结构体成员；生成器自己的 wrapper（`&line`、`&out`）就会误报。必须改为基于符号表或直接交给编译器 |
| E-07 | 可判定 | 双 trait 顺序 |
| E-08 | **不可判定** | 四种组合里「子命名空间/跨命名空间」在命名空间单元下 CT 直接报错，规则本身不成立（§3.5） |
| F-01 | 可判定 | 脚本存在 + 示例违规命中 |
| F-02 | 可判定 | ≤5 步 + diff |
| F-03 | **不可判定** | 「业务代码不需要读 Bridge」是主观判断。应改为「`js/app/**` 中 `grep -c 'nexcore_'` 为 0」等具体断言 |
| F-04 | 条件可判定 | 「CI 或 review 拦截」含人工；作为演练可接受，但需写明「5 个里至少 3 个必须由 CI 拦截」 |
| G-01 | 条件可判定 | 「全部路径」需枚举；泄漏判据需固定为 ASan 配置 |
| G-02 | **不可判定** | 「重建后 runtime 正常」→ 改为「create 返回 0 且一次 ping 往返成功」 |
| G-03 | 可判定 | 三方 hash 相等 |
| G-04 | **不可判定** | 「fd/线程数稳定」无常数 → 改为「fd 数与基线相等、线程数恒为 3、RSS 增量 < 5%」 |
| G-05 | 条件可判定 | 见下节性能协议；且依赖 Q-16，设备未定时只能条件通过 |
| G-06 | 可判定 | 关闭顺序 + 错误码 |
| H-01 | **不可判定** | 「内存不无界增长」→ 给出迭代数、RSS 上限、观测点 |
| H-02 | 可判定 | 静态扫描 |
| H-03 | 可判定 | 降级路径 |
| H-04 | 条件可判定 | 「密钥」需在 fixture 中定义具体字符串 |
| H-05 | 条件可判定 | 「抛异常**或**中断」两个互斥期望并列，需二选一 |

**性能测量协议（G-05 与验收 §10）**：已有环境、载荷、预热、采样、起点终点、分位数，是这份文档里少数接近可直接执行的部分。仍缺 4 项：

1. **并发度未写**。现在测的是「1 个串行往返」，但没写「并发 = 1」，也没写吞吐条目（req/s）。多线程压测与串行延迟是两个不同的数。
2. **统计定义未写**。p99 是否含全部 100k 样本、是否剔除首轮、是否多轮取中位、是否剔除离群值，都没说。同一台机器两种算法能差 20%。
3. **是否计入 JS handler 业务耗时**未写。起点是 `send` 返回、终点是 sink 收到响应，但没说明这中间是否执行真实 handler；如果 handler 是空函数，测的是传输；如果是业务 handler，测的是端到端。两者必须分开报。
4. **预算未版本化**。阈值写「暂定」，没有落到 `decisions.md` 的编号决策，也没有生效版本。应新增一条决策记录，写明阈值、适用设备、生效版本、变更流程。

另：G-05 依赖 Q-16 设备型号；Q-16 未定时，本条只能判「条件可判定」，不能算通过。

### 3.2 编号系统

README 的编号约定确实提示了「同一缩写在不同文档含义不同」，但它自己的表只列了两种 `G-xx`，漏了第三种：

| 前缀 | 含义 1 | 含义 2 | 含义 3 |
| --- | --- | --- | --- |
| `G-xx` | design §5.6 已知缺口 G-01..G-14 | codegen §8 生成器规则 G-01..G-18 | **acceptance 平台集成 G-01..G-06** |
| `F-xx` | design §4 固定模式 F-01..F-15 | acceptance AI 友好 F-01..F-04 | — |
| `D-xx` | decisions 决策 D-01..D-14 | acceptance 事件 D-01..D-04 | — |
| `R-xx` | codegen §8.1 符号解析 R-01..R-03 | review-criteria 可审阅规则 R1–R9 | — |

结论：**需要整改（P1-10，文档问题）**。理由不是「看着乱」，而是有具体的误读路径：

- `acceptance.md` E-04 写「运行 G-01..G-18 与 R-01..R-03」，而同一个文件里就有自己的 `G-01..G-06`。读者按就近原则理解，会去找 6 条平台集成项，找不到第 7–18 条。
- `roadmap.md` P3 同样写「G-01..G-18」，没有文档名。
- `design.md` §5.6 的 G-05/G-13 指向「生成器规则 G-16/G-17」，而 design 自身只到 G-14；读者在 design 里翻不到。

README 第 52 行已经规定「引用时必须带文档名」，但 E-04/P3/§5.6 三处都违反了这条自己的规定。所以这不是「命名本来该分开」就能豁免的，是**规则与执行不一致**。

**整改方案（推荐 A）**：

| 方案 | 做法 | 迁移成本 | 评价 |
| --- | --- | --- | --- |
| A | 缺口 `GAP-xx`、生成规则 `GEN-xx`、符号解析 `SYM-xx`、固定模式 `FIX-xx`、验收 `ACC-<组>-xx`、决策 `D-xx`、待确认 `Q-xx`；评审口径的 `R1–R9` 改为 `CR-R1..CR-R9` | 一次性全文替换 + 5 处引用 | 推荐。前缀自带语义，不依赖文档名 |
| B | 保留前缀，全部写成 `codegen:G-16` | 只改引用点 | 成本低，但仍需读者先看文档名；且 `G-16` 在 acceptance 里不存在，搜索仍会串 |

无论选哪个，必须同步修改：`README.md` 编号表、`acceptance.md` E-04、`roadmap.md` P3、`design.md` §5.6 的 G-05/G-13、`codegen.md` §8 的 R 系列标题。

### 3.3 16 条 RC 是否覆盖两阶段关闭

先给结论：**不覆盖**。四类在途对象逐类核对：

| 在途对象 | 是否有 RC / 验收 | 缺口 |
| --- | --- | --- |
| 在途请求（JS pending） | 有（C-05、RC 相关条目） | 只覆盖 JS 侧 pending；**outbox-response 里已生成、尚未被 drain 取走的响应**没有归属 |
| 在途事件 | **没有** | `$shutdown` 后 outbox-event 里残留的事件是被投递、丢弃还是报错，全文未定义 |
| 正在泵的微任务 | 部分（§7.1 有预算、RC-16 有中断） | 但关闭时的「拒绝 pending」与「泵空」先后矛盾（见下） |
| inbox 中已接受未处理的请求 | **没有** | `send` 返回 0 表示已接受；关闭时这些请求没有任何终态，也没说调用方应看到什么 |

**P0-1：关闭 turn 的顺序三处不一致。**

| 文档 | 顺序 |
| --- | --- |
| design §7 | 拒绝 JS pending → 发 `$shutdown` → 泵空微任务 → 释放 `JSValue` |
| protocol §12.6 | 发 `$shutdown` → 拒绝 JS pending → 泵空微任务 |
| async-threading §7.1 | 发 `$shutdown` → 泵空微任务 → 拒绝 JS pending → 清空 C pending |

三种顺序的后果不同：

- 按 design（先拒绝后泵）：拒绝产生的 reaction job 会被泵到，`.catch` 能执行；但 `$shutdown` 通知在泵之后才发，JS 侧可能先看到 `E_SHUTDOWN`、再看到 shutdown 事件。
- 按 async-threading（先泵后拒绝）：拒绝产生的新 job 留在队列里，`.catch` **永远不会执行**，随后 runtime 被释放。
- 按 protocol（先拒绝后泵）与 design 类似，但没有「清空 C pending」这一步。

必须定死一条，并补一句「拒绝后是否再泵一轮、最多几轮」。我建议：`$shutdown` → 拒绝 pending → 泵空（复用同一预算）→ 清空 C pending → 释放；同时把「关闭阶段的拒绝不再触发业务 `.then`」写进契约。

**「resolve 之后 JS 已被销毁」**：由于 JS 侧全部在专用线程串行执行，正常路径不会出现。真正的风险在 C 侧：

**P0-4：`nexcore_free` 与 drain 的 TOCTOU。** design §7 要求「没有 drain 正在执行」才能 free，RC-08 也这么写；但「检查 `drain_active == 0`」与「销毁 mutex/队列」之间不是原子的。drain 线程可以在这两步之间进入，随后对已释放的 mutex 加锁。必须在同一把生命周期锁内完成「置 DEAD + 等 drain 退出 + 销毁」，或要求调用方先 `shutdown` 再 `free` 且 `shutdown` 已 join 全部线程。

**P0-5：`fatal shutdown` 未定义。** async-threading §5.2 与 Q-10 都提到「超预算 → `E_JS_RUNAWAY` 并 fatal shutdown」，但 design §7 的状态机只有 RUNNING / STOPPING / DEAD：谁触发 fatal、句柄是否直接 DEAD、`send`/`drain` 之后返回什么、平台 pending 如何终结，全是空的。C-06 的「可干净重建」也因此无法判。

### 3.4 19 个错误码与协议是否自洽

计数复核：**19 个，数字正确**。超时（`E_TIMEOUT`）、取消（`E_CANCELLED`）、背压（`E_BACKPRESSURE`）都有，这一点达标。

问题：

| 编号 | 级别 | 问题 |
| --- | --- | --- |
| P0-3 | P0 | `$cancel { target: uint32 }` 无方向。protocol §3.1 规定两个方向各自从 1 分配 id，因此 UI→JS 的 7 与 JS→UI 的 7 可以同时存在，取消会打错对象。修法：`target` 改为 `{ direction, id }`，或两个方向用不同的 id 段 |
| P1-3 | P1 | `E_NOT_SUBSCRIBED`（§4）与 `$unsubscribe`「未知 id 忽略、重复调用不报错」（§6.1）互斥。按 §6.1，这个码永远触发不了；按 §4，`$unsubscribe` 又该返回错误响应。二选一，并同步 `B-07`/`D-01` |
| P1-4 | P1 | 缺「结果不可序列化」的码。handler 返回函数、Symbol、BigInt、循环引用时，`E_INVALID_PARAMS` 不适用（那是入参），`E_INTERNAL` 会掩盖协议问题。建议新增 `E_RESULT_UNSERIALIZABLE`（不可重试），并在 §8 写明「序列化失败必须 reject，不得静默转 `null`」 |
| P1-5 | P1 | `result`+`error` 同时出现时，§13 的处理是「丢弃 + `$error(E_INVALID_REQUEST)`」。发起方因此收不到响应，只能等超时；而 B-01 要求「负例返回对应错误码」。要么立即以 `E_INVALID_REQUEST` 失败，要么在 §13 明写「发起方以超时终结」 |
| P2-1 | P2 | 协议码与 ABI 码有 5 个同名：`E_INTERNAL`、`E_SHUTDOWN`、`E_BACKPRESSURE`、`E_PAYLOAD_TOO_LARGE`、`E_SCHEMA_MISMATCH`。数值域不同不构成错误，但日志里无法区分「ABI 拒绝」与「协议拒绝」。建议协议码统一加前缀或日志里带域 |
| P2-2 | P2 | `E_CANCELLED` 标 `retryable: true`，与 §9.2「取消是终态」冲突。既然 `retryable` 只是建议，也应改为 `false` |
| P2-3 | P2 | 只有 `id`、既无 `method` 也无 `result`/`error` 的信封未定义。§13 只覆盖了「缺 method」和「result+error 同时出现」 |
| P2-4 | P2 | 「超过容量时产生**一次** `E_BACKPRESSURE`」的「一次」作用域未定义（每次溢出 / 每个丢弃事件 / 每次 drain），D-03 据此断言「一次」，但无法验证 |

`result | error` 的互斥**是强制的**：§2.2/§2.3 分别写了「不得同时带」，§13 也有处理。这一点通过。

### 3.5 生成器规则能否机械检查

`codegen.md` §8 有 18 条 G + 3 条 R。逐条归类后：

| 规则 | 检查方式 | 判定 |
| --- | --- | --- |
| G-01/G-02/G-03/G-11/G-18 | `head`、`--check`+diff、`grep`、CI | 可机械检查 |
| G-04/G-07/G-08 | `grep` + 目录白名单 | 可机械检查（需维护白名单） |
| G-06/G-12/G-13/G-15 | 单测、表项计数、负例、`cc -Werror` | 可机械检查（G-13 的规则本身错误，见下） |
| G-09 | 两次构建 diff | 可机械检查 |
| **G-10** | 「代码审查 + CI 沙箱」 | **只能人读** |
| **G-14** | 「生成器模板审查」 | **只能人读** |
| G-05 | 「脚本统计行数 + AST/正则」 | 名义有脚本；用正则算复杂度会在字符串、宏、注释上误判，需要 AST |
| **G-16** | 「`&ident` 的 ident 必须在本单元定义」 | **脚本不可靠**：正则无法区分局部变量、参数、结构体成员、宏参数。生成器自己的 wrapper 里 `&line`、`&out` 就会命中。必须改成基于符号表的检查，或直接依赖编译器 |
| **R-03** | 「正则收集标识符引用 + 本单元定义 + 白名单」 | **不可靠且与 G-15 重复**：C 的宏、typedef、局部变量使正则必然误报；`cc -fsyntax-only` 已经能做这件事，建议删掉 R-03 或明确「R-03 = 编译器前端」 |

**P0-2：G-13/G-17/R-01 与 CT 事实相反。** 规则原文要求：「同命名空间调用本单元函数可用限定名；调用 import 进来的函数用裸名；跨命名空间才全限定」。我在 `/tmp/ctrepro3` 逐条实测（命令与输出见 §5）：

| 场景 | 写法 | CT 结果 | 生成 C | `cc` |
| --- | --- | --- | --- | --- |
| 同单元内调用 | `s::k(v)` | exit 0 | `s_k(v)` | 通过 |
| 同命名空间、**跨单元** | `k(v)`（裸名） | exit 0 | `k(v)`，**不生成依赖 `#include`** | `call to undeclared function 'k'` |
| 同命名空间、跨单元 | `s::k(v)`（限定） | **exit 1** | — | — |
| 跨命名空间、命名空间单元 | `h(v)`（裸名） | exit 0 | `h(v)`，无 include | `call to undeclared function 'h'` |
| 跨命名空间、命名空间单元 | `s::t2::h(v)`（全限定） | **exit 1**（`no function of that name in that namespace`） | — | — |
| 跨命名空间、**根单元** | `s::t2::h(v)` | exit 0 | `s_t2_h(v)` + `#include "mod.h"` | 通过 |

补充：在命名空间单元里，未声明的函数调用**完全不做校验**（写 `zzz(v)` 也 exit 0、原样发射）；而同一位置的**变量**引用会直接报 `use of an undeclared variable`。即函数位置比变量位置更宽松，这正好是 §3.7 静默错译的成因。

结论：

- `G-17` 第 1 条只对「同一单元内定义的函数」成立，对导入函数不成立。
- `G-17` 第 2 条（导入函数用裸名）**是错的**，且会把审计 §3.7 的 P0 直接写进生成器。
- `G-17` 第 3 条（跨命名空间全限定）在命名空间单元里**根本执行不了**。
- 因此 `R-01`（生成 CT 源码时同命名空间用裸名）、`G-13`、以及 E-08 的四种组合全部要重写。

可用的替代路径只有三条，文档必须挑一条写死：

1. **Bridge 全部放在根级单元**（无 `namespace`），跨单元用 `a::b::mod::f`，CT 会补 include、改写 cname。这是唯一实测可编译的多单元写法。
2. **Bridge 收成单文件单元**，单元内用裸名。
3. **用 `#ct-raw` 手写 `extern` 声明 + 直接写 cname**（`x_y_mod_f`），但这与 R5「业务逻辑不得藏在 raw」冲突，只能作为过渡。

### 3.6 `NexCoreConfig` 是否真的冻结得住

结构体本身是「启动参数 + 版本 + 预留」，不会随业务增长而膨胀，方向正确。但冻结性有 5 个洞：

| 编号 | 级别 | 问题 |
| --- | --- | --- |
| P1-6 | P1 | `struct_size` 的判据没写。§6.3 只说「不匹配 → `NX_E_INVALID_ARGUMENT`」，按常规读法是严格相等。若是严格相等，任何新增字段都会让旧库拒绝新宿主，`abi_version` 永远只能升、无法渐进；`reserved` 也就没有意义。应明确：`struct_size >= 已知最小长度` 时接受，尾部未知字段忽略；只有破坏性变更才比对 `abi_version` |
| P1-6 | P1 | `reserved` 没有使用规则：新库能否读、旧库如何无视、填 0 是否强制。冻结前必须写 |
| P1-7 | P1 | 没有**时钟注入**（也没有分配器、日志回调）。G-04（24h 泄漏）与 G-05（p99）只能依赖墙钟，CI 不可复现；以后要补只能改 ABI。建议现在就加 `now_ms` 回调（可空，默认 `clock_gettime`） |
| P2-5 | P2 | `bootstrap_js` 用 `const char *` + `size_t`，32/64 位布局不同。`_Static_assert` 必须按架构分别写；Swift/Kotlin 侧没有任何布局自检，A-04 的「CI 失败」没有实现路径（P1-13） |
| P1-8 | P1 | `schema_hash` 是**单个 NUL 结尾字符串**。若 Q-14 最终选「多版本共存/能力集」，字段形状（数组或指针+长度）或语义必须变，属于 ABI 破坏。Q-14 不能等到 P3 |

结论：**冻结得住，但前提是先修正 `struct_size` 语义、补时钟注入、并在 ABI 冻结前定 Q-14。**

### 3.7 交叉引用抽查（10 处）

| # | 引用方 | 引用 | 目标实际内容 | 判定 |
| --- | --- | --- | --- | --- |
| 1 | protocol §6.1 | 「见 §6.4」 | §6.4 是顺序与投递保证，不讨论 subId 过滤；应在 §6.3（快照与退订） | **错引** |
| 2 | codegen §3.3 第 9 条 | 「见 §7」 | §7 是命名规则；破坏性变更在 §9.2 | **错引** |
| 3 | design §3.3 第 7 条 | 「见 §5.3」 | §5.3 是结构体与 trait 语义；平台条件编译/ shim 在 §5.4 | **错引** |
| 4 | open-questions Q-03 | 「protocol.md §2」 | §2 是信封；int64 wire 规则在 §3.3 | **错引**（同一问题两处） |
| 5 | open-questions Q-07 | 「codegen.md §8」 | §8 是检查规则表；Kotlin/Native 不在范围写在 §5.2 | **错引** |
| 6 | README §编号约定 | 只列两种 `G-xx` | 实际三种（漏 acceptance G 组） | **不完整** |
| 7 | design §3.4 | 「D-13」 | D-13 是 UI 不直连 Core | 正确 |
| 8 | design §5.6 G-02 附近 | 「§5.6 G-12」 | G-12 是跨单元 `&f` | 正确 |
| 9 | open-questions Q-20 | 「acceptance.md §10」 | §10 确实是性能协议 | 正确 |
| 10 | roadmap P4 | 「G-01..G-06、H-01..H-05」 | 与 acceptance 对应 | 内容正确，但裸写前缀 |

10 处里 5 处错引、1 处不完整、4 处正确。整改要求：所有跨文档引用写成「文件名 + 小节标题」，禁止只写 `§x`；编号前缀按 §3.2 的方案统一。

### 3.8 Q-01..Q-20 的临时默认是否安全

按「选错后的返工成本」分三档：

**必须前移到 P1 出口（ABI 冻结前）——5 条：**

| 编号 | 默认 | 为什么不能等 |
| --- | --- | --- |
| Q-14 | A（严格 hash 相等） | 直接决定 `NexCoreConfig.schema_hash` 的形状与语义；改选 B 就是 ABI 破坏 |
| Q-12 | B（后台挂起需 `nexcore_pause`） | 文档自己承认要加第 8 个 ABI 函数；ABI 冻结后再加就是 v2 |
| Q-03 | B（int64 走 number，±2^53） | 决定 Schema 类型映射、校验、全部生成绑定与 AI 模板；改一次等于协议大版本 |
| Q-13 | 同信任、非对抗 | 决定中断、内存上限、是否签名；影响 F-15 与性能预算，且 iOS 合规（Q-02）依赖它 |
| Q-06 | SPM/静态库 + JNI | P1 就要产出 Swift 冒烟与 JNI 壳；晚定会重做打包与 CI |

**维持现状即可（只影响局部）：** Q-07、Q-11、Q-17、Q-18、Q-19。

**现状可接受但需加约束：**

- Q-01（QuickJS 版本）：deadline 写「P2 编码前」可以，因为 P1 不接 QuickJS；但应补一句「P1 出口前锁定候选版本，P2 的 shim 接口按它设计」。
- Q-16（性能设备）：必须在 P4 首次基准前定，且 G-05 在此之前只能判条件通过。

---

## 4. 必须整改清单

### P0（阻断）

1. **统一关闭顺序**（P0-1）：三份文档改为同一顺序，并写明「拒绝后是否再泵一轮」。建议顺序：`$shutdown` → 拒绝 pending → 泵空（同一预算）→ 清空 C pending → 释放。
2. **重写命名空间调用规则**（P0-2）：删除 G-13/G-17/R-01 现有表述，改为 §3.5 三条路径之一，并补一条最小实验进 E 组验收。
3. **`$cancel` 加方向**（P0-3）：`{ direction, id }` 或分段 id；同步 §3.1、§5.1、§6.3、§12.4。
4. **修 `free`/drain 原子性**（P0-4）：在 `shutdown` 内 join 全部线程，`free` 只接受 DEAD 句柄；或加生命周期锁覆盖「检查 + 销毁」。
5. **定义 `fatal shutdown`**（P0-5）：谁触发、状态迁移、`send`/`drain` 返回码、pending 终结方式、句柄是否可重建。

### P1（里程碑前）

6. 四类在途对象的收尾表（P1-1）：补 outbox-response、outbox-event、inbox 已接受请求三条 RC 与验收。
7. 调整泵预算或 C-03（P1-2）：把 `MAX_JOBS` 提到 20,000，或把 C-03 改为 5,000 长链。
8. 错误码三处修正（P1-3/4/5）：`E_NOT_SUBSCRIBED` 二选一；新增结果序列化失败码；`result+error` 的发起方终态写清。
9. `NexCoreConfig` 三处补写（P1-6/7）：`struct_size` 比较规则、`reserved` 规则、时钟注入。
10. Q-14/Q-12/Q-03/Q-13/Q-06 前移到 P1 出口（P1-8），并在 `decisions.md` 各立一条决策。
11. 验收改写（P1-9）：11 条不可判定全部给出可执行判据；14 条补枚举/种子/阈值。
12. 编号整改（P1-10）：按方案 A 全文替换，并修 5 处引用点。
13. 生成器规则整改（P1-11）：G-10/G-14 标注「人工审查，不计入 CI 通过条件」；G-05 改 AST；G-16 改符号表/编译器；R-03 合并进 G-15。
14. 补两条验收（P1-12）：`E_SCHEMA_MISMATCH` 的三方不一致；同一 handle 两个线程 `drain` → `NX_E_THREAD`。
15. 补布局门禁（P1-13）：提交 `abi-layout.txt` 黄金文件；Swift/Kotlin 各加一处 `sizeof` 断言。
16. 补 C1 门禁（P1-14）：`grep -r 'quickjs\.h\|JS_'` 白名单；`nm` 检查只有 `NexCore.h.ct` 导出 `nexcore_*`。

### P2（待办）

17. 协议码与 ABI 码同名（P2-1）：日志带域。
18. `E_CANCELLED` 的 `retryable` 改 `false`（P2-2）。
19. 只带 `id` 的信封补规则（P2-3）。
20. 「一次 `E_BACKPRESSURE`」定义作用域（P2-4）。
21. `bootstrap_js` 的 32/64 位断言（P2-5）。
22. 5 处错引修正（§3.7）。
23. F-04 写明「至少 3 个由 CI 拦截」。
24. `ai-friendly.md` §1「`docs/**` AI 可自由生成」与 design §11、CODEOWNERS 的文档所有权冲突，需统一（建议：`docs/design.md`、`protocol.md` 与决策记录列为「AI 禁止直接修改」）。
25. `D-01` 与 `B-07` 内容重复，合并。

---

## 5. 证据附录：CT 跨单元调用复跑

环境同审计：`ct v0.19.1`、commit `c27da3d`、macOS arm64。目录 `/tmp/ctrepro3`，不写入本项目。

### 5.1 复现文件

```c
/* s/mod2.h.ct */
namespace s;
pub int k(int v) { return v + 3; }

/* s/app2_bare.h.ct —— 同命名空间、跨单元、裸名 */
namespace s;
import mod2;
pub int g(int v) { return k(v); }

/* s/app2_qual.h.ct —— 同命名空间、跨单元、限定名 */
namespace s;
import mod2;
pub int g(int v) { return s::k(v); }
```

### 5.2 命令与结果

```
$ ct build --emit-c --ns-root . -I . s/app2_bare.h.ct --outdir out/same
cc -std=c99 out/same/app2_bare.c -o a.out
$ sed -n '1,6p' out/same/app2_bare.c
#include "app2_bare.h"
...
int s_g(int v) {
    return k(v);
}

$ cc -std=c99 -Wall -Wextra -Werror -c out/same/app2_bare.c -o out/same2.o
s/app2_bare.h.ct:3:12: error: call to undeclared function 'k'; ISO C99 and later
        do not support implicit function declarations [-Wimplicit-function-declaration]
    3 |     return k(v);
      |            ^
1 error generated.

$ ct build --ns-root . -I . s/app2_bare.h.ct -o out/bin/app2
ct_build_exit=1          # 同上 cc 错误

$ ct build --emit-c --ns-root . -I . s/app2_qual.h.ct --outdir out/sameq
s/app2_qual.h.ct:3:31: error: no function of that name in that namespace
```

### 5.3 对照：根单元

```c
/* cross_root.ct */
import s::t2::mod;
pub int g(int v) { return s::t2::h(v); }
```

```
$ ct build --emit-c --ns-root . -I . cross_root.ct --outdir out/c_root
cc -std=c99 out/c_root/cross_root.c out/c_root/mod.c -o a.out
$ grep -n include out/c_root/cross_root.c
3:#include "mod.h"
$ tail -3 out/c_root/cross_root.c
int g(int v) {
    return s_t2_h(v);
}
```

结论：**只有根单元能完成「限定名 + 自动 include + cname 改写」这条链路**；命名空间单元在函数调用位置既不校验、也不改写、也不补 include。

### 5.4 补充观察

- 命名空间单元内写 `zzz(v)`（完全未声明）也 exit 0 并原样发射，说明该位置不做符号校验。
- 同一位置引用未声明**变量**会报 `use of an undeclared variable`，比函数位置严格。
- `ct build --emit-c` 只打印 `cc` 命令行，不执行；本次所有「生成成功」结论都以真实 `cc`/`ct build` 退出码为准。

---

## 6. 与本项目其他评审文档的关系

- 语言事实以 [ct-capability-audit.md](ct-capability-audit.md) §3.5/§3.7/§3.8 为准；本文 §3.5 新增的「命名空间单元无法跨单元调用」是对审计的补充，不改变 §3.7 的结论。
- 判据与证据要求见 [review-criteria.md](review-criteria.md) §1、§9；本文的 P0 判定按该文件的门槛执行。
