# 验收标准

本文把“能跑”换成可判定的条目。每条都给出**方法、阈值、证据**；没有证据视为未通过。性能阈值在 P4 首次基准后固化，之前标“暂定”。

## 0. 验收分层与归属

框架只对**框架能力**负责，不对业务结果负责。验收分三层：

| 层级 | 编号 | 归属 | 是否阻塞框架发布 |
|---|---|---|---|
| 框架验收 | `ACC-A..H` | 框架 | **是**（MUST） |
| 验证样例 | `SAMPLE-01..02` | 示例 app（验证框架可用，**不是交付物**） | 否；只作为 G 组的载体证据 |
| 宿主验收 | `HOST-01..04` | 宿主项目 | 否；由宿主项目自己的 CI/测试负责 |

判定原则：

1. 每条 `ACC-*` 的结论必须能在“业务实现替换为 stub”后仍然成立；依赖业务结果的断言不属于框架验收。
2. 框架验收只断言：调用送达、参数符合 Schema、线程/顺序/超时/取消/关闭语义、错误码、生成物一致、边界门禁。
3. 业务规则正确、状态机跳转正确、计价结果、UI 交互与视觉，全部归 `HOST-*`，框架不验收。
4. 样例 app 是 `SAMPLE-*` 的载体；样例通过不等于框架通过，反之亦然。

### 0.1 与原 57 条的对应关系

| 原编号 | 处置 | 说明 |
|---|---|---|
| ACC-G-01 | **改写为框架断言** | 原“SwiftUI demo 走通业务路径”依赖宿主业务与 UI；现只断言 transport、sink 线程、超时/取消/关闭。业务结果移到 HOST-01/03，样例载体为 SAMPLE-01 |
| ACC-G-02 | **改写为框架断言** | 原“Compose demo + 业务恢复”；现只断言进程重建后 `schema_hash` 一致、2 s 内首次请求、100 次往返成功、无泄漏。业务状态恢复移到 HOST-02，载体为 SAMPLE-02 |
| ACC-F-02 | **改写为 fixture 流程** | 原“新增一条业务方法并测试通过”；现用 `tests/fixture.schema.yaml` 的新增 echo 方法验证四端生成与协议往返，业务实现移到 HOST-03 |
| ACC-E-05 | **改写为 fixture 统计** | 原统计对象是宿主业务方法集；现固定 fixture Schema，避免验收依赖宿主内容 |
| ACC-E-08 | **改写为映射单元拓扑** | 原根转发正例作废；改为 `<ns>_map.m.ct` 双向正例 + 无映射三种负例 + 跨单元 `&f` 仍需 wrapper（D-15 变更） |
| ACC-C-09 | **判定澄清（不改编号）** | 原“free 后旧句柄返回 `NX_E_INVALID_HANDLE`”与 design §7 的“free 后调用是 UB”冲突；改为在 `shutdown` 后、`free` 前断言 `NX_E_SHUTDOWN`，`free` 后不再调用 |
| 其余 51 条 | **保持编号与判定** | 均为框架能力/ABI/协议/线程/生成/边界断言，不依赖业务语义 |

> 计数：57 条 `ACC-*` 全部保留编号；其中 4 条因“定位分层”改写（G-01、G-02、F-02、E-05），1 条因 D-15 拓扑变更改写（E-08），1 条判定澄清（C-09），其余 51 条不变。

## 1. 判定规则

- 所有标 **MUST** 的条目必须通过；任一失败即阶段不通过。
- 崩溃、ASAN/TSAN 报告、泄漏、双重 settle、响应静默丢失、Schema 漂移、边界检查失败，均直接判失败，不允许“已知问题”豁免。
- Flaky（同一命令连续 3 次有 1 次失败）= 失败；必须先修稳定性。
- 机械检查（`tools/check.sh`）以**真实运行**的 `SCRIPT_RC=0` 与逐项 PASS 输出为通过；静态推演、部分执行、脚本未跑通都不算通过。当前全部检查项（`0`、①a~①c、②a~②c、③、④、⑤a、⑤b、⑥、smoke）已由编码侧全量实跑通过，且每条都有真失败过的负例。
- 证据格式：

```text
[条目] ACC-A-01
[环境] macOS 15.x / Apple silicon / clang 16 / CT c27da3d / QuickJS <commit>
[命令] <完整命令>
[结果] PASS/FAIL
[输出] <关键 stdout/stderr，或日志路径>
[产物] build/reports/ACC-A-01.txt（或截图/二进制）
[时间] 2026-11-06T12:00:00+08:00
```

- 性能数据必须附：设备型号、OS、构建类型（Debug/Release）、payload 大小、样本数、warmup 数。
- 不允许用 Debug 构建的数据声称 Release 性能；不允许用模拟器数据声称真机性能。

## 2. A：ABI 与构建

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-A-01 | 发布头可被严格 C 编译且只含 ABI | Pass 1 只编 `src/NexCore.h.ct`，产出 `build/abi/NexCore.h`；`cc -std=c99 -Wall -Wextra -Werror -fsyntax-only -I build/abi build/abi/NexCore.h`；比对前置声明 | 退出码 0，无 warning；发布头中出现的 struct 名**只有 `NexCoreConfig`**（前置声明与定义都算）；`grep -nE '^struct [A-Za-z0-9_]+;' build/abi/NexCore.h` 不得出现 `queue_Ring`/`protocol_*`/`NexCoreHandleImpl`；`abi/expected-forward-decls.txt` 只允许 `struct NexCoreConfig;` 一行；不存在其他 `struct <name> {` | 命令输出 + golden diff |
| ACC-A-02 | ABI 符号白名单 | `nm -gU build/libnexcore.dylib` / `nm -D libnexcore.so` | 只出现文档列出的 7 个 `nexcore_*`；无 C++ 符号；无未文档化的全局 | `nm` 输出 + 白名单 diff |
| ACC-A-03 | 跨 ABI 结构体无 trait/vptr | 生成器负例：`pub struct X { int a; } use T` + 按值嵌套负例 | 生成器拒绝；`NexCore.h` 不含 `use`/`vptr`；`_Static_assert(sizeof/offsetof)` 通过 | 负例日志 + 头文件 grep |
| ACC-A-04 | ABI 版本/布局冻结 | 修改 `struct NexCoreConfig` 字段但不升 `abi_version`，跑 ACC-A-10 的 golden 比对；另构造“大 `struct_size` + 尾部垃圾”的调用方结构体 | ACC-A-10 产生 diff 且 CI 失败；**负例实现**（严格相等 `struct_size`）在单测中必须被拒绝（防回归）；`struct_size < MIN` 或 `reserved != 0` 返回 `NX_E_INVALID_ARGUMENT`；`struct_size == MIN` 与 `> CURRENT` 都正常创建，尾部被忽略 | golden diff + 单测 + 编译期断言 |
| ACC-A-05 | Swift 可导入头并调用 | `swiftc -import-objc-header build/abi/NexCore.h` 编译最小程序，调用 create/stat/drain/free | 退出码 0，运行无崩溃 | Swift 测试日志（已实测可行，作为回归） |
| ACC-A-06 | Kotlin/JVM 可加载 JNI | `kotlinc` 编译最小程序，`System.loadLibrary` + `nexcore_stat` | 退出码 0，返回 `NX_OK` | JVM 测试日志（已实测 JVM；Kotlin/Native 不验收） |
| ACC-A-07 | Android NDK 构建 | `./gradlew :nexcore:assembleRelease`，检查 `libnexcore.so` | arm64-v8a/x86_64 均产出；`readelf -d` 只依赖 libc/libm/liblog/libdl | 构建日志 + `readelf` |
| ACC-A-08 | 多线程 drain | 两个线程用 barrier 同时从 `NX_OK` 开始调用 `nexcore_drain(-1)` | 恰好一个进入阻塞并收到消息；另一个确定返回 `NX_E_THREAD`（不依赖调度顺序）；无重复 | 单测（barrier + 重复 100 次） |
| ACC-A-09 | 时钟注入 | 传入假 `now_ms`，推进 24h | 超时/预算全部按假时钟触发，不依赖墙钟；真实等待 ≤ 100 ms | 单测 |
| ACC-A-10 | 布局 golden | 生成 `build/abi/abi-layout.txt`（每个 ABI 类型的 size/offsetof），与仓库中 `abi/abi-layout.txt` 比对 | 任一字段增删/重排导致 diff；CI 失败且要求升 `abi_version` | diff + CI 日志 |
| ACC-A-11 | Schema 不匹配 | 修改 `schema_hash` 后 `create`；随后在同一进程重试正确 hash 的 `create` | 返回 `NX_E_SCHEMA_MISMATCH`，`*out==NULL`；失败路径无 JS 线程残留（重试 create 成功即证明资源已回收）；ASAN/LSAN 无泄漏 | 单测 + ASAN |
| ACC-A-12 | 统计键编号与“已分配未实现”行为 | 运行 `tests/stat_keys.c`（check.sh ⑥）：`*out` 预填哨兵（`10..14` 组填 `-424242`，未分配组填 `-7`），对 `10..14` 各调用一次 `nexcore_stat`；另调 `0`、`19`、`20`、`1024`；再抽样 `1..9`、`15..18` | `10..14` 返回 `NX_E_INTERNAL` 且 **`*out` 未被改写**；`0`/`19`/`20`/`1024` 返回 `NX_E_INVALID_ARGUMENT`；`1..9`、`15..18` 返回 `NX_OK` 并写入值。编号表以 design §6.4 / D-20 为唯一来源。**计数正确性不属于当前验收范围**：`10..14` 对应的关闭流程、JS 线程、drain pump 尚未落地，没有事件源，伪造采样会破坏“已分配未实现”的语义；待对应子系统落地后随事件源实现，届时再把它扩入本条 | 单测 + 日志（已随 check.sh ⑥ 实跑通过） |

## 3. B：协议一致性

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-B-01 | 信封合法性 | 固定用例文件 `tests/envelope-v1.json`：30 条正例 + 30 条负例（缺 id、id=0、params 为数组、result+error、未知方法、错误方向、超限、坏 JSON 各 ≥3 条，共 60 条） | 正例全成功；负例返回对应错误码（含 `result+error` → `E_INVALID_REQUEST`）；无崩溃 | 测试报告 |
| ACC-B-02 | 错误码稳定 | 对每个错误码构造触发条件，断言 `code` 精确匹配 | protocol.md §4 的 19 个错误码全部命中；`message` 不参与断言 | 测试报告 |
| ACC-B-03 | id 匹配 | UI→JS 与 JS→UI 各 10k 并发请求；固定种子 `id-v1` 生成 id，经可注入调度器（`tests/sched.c`）按逆序/随机/固定延迟三档回包 | 每个 Promise/continuation 恰好 settle 一次，结果与 `(direction,id)` 对应；无串扰 | 测试报告 + 不变量断言 |
| ACC-B-04 | 超时与晚到响应 | 1 ms/50 ms 超时；超时后真实响应到达 | 发起方收到 `E_TIMEOUT`；晚到响应被丢弃；`NX_STAT_DROPPED_LATE` 递增；无第二次 settle | 测试报告 |
| ACC-B-05 | 取消 | `$cancel` 在响应前/后/竞跑 1000 次；竞跑用 barrier 固定三种时序（先取消、先响应、同 turn 各 1/3） | 取消前：`E_CANCELLED` 恰好一次；取消后：原响应正常；竞跑：无双重 settle | 测试报告 |
| ACC-B-06 | 通知无响应 | 发 `$emit`/`$error` 通知 | 不产生响应；错误走 `$error` + 统计 | 测试报告 |
| ACC-B-07 | 订阅语义 | 快照、emit 中订阅/退订、重复退订、未知 subId | 新订阅不收当前 emit；退订后不再调用；重复退订与未知 subId 均返回 `null` 且不产生错误（幂等，无 `E_NOT_SUBSCRIBED`） | 测试报告 |
| ACC-B-08 | 事件顺序 | 单 emitter 100k 事件 | 保序、不重、不丢（容量内） | 测试报告 |

## 4. C：异步与线程

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-C-01 | 线程归属 | 在 JS handler、sink、Core handler 中记录线程 ID | JS handler 只在 JS 线程；sink 只在 drain 线程；Core 在 JS 线程；无交叉 | 测试报告 |
| ACC-C-02 | MPSC 投递 | 8 线程 × 10k `nexcore_send`，同时 drain | 所有“已接受”消息最终到达；拒绝均有明确错误码；无丢失、无重复、无崩溃 | TSAN + 计数断言 |
| ACC-C-03 | 微任务泵送顺序 | 构造 `Promise.resolve().then` 10k 链，然后发下一条 native 消息；用**假时钟**推进，避免真实 200 ms 预算干扰 | 10,000 个 continuation 全部执行且顺序正确；允许跨 continuation turn（10k 配额是让出，不是失败）；下一条 native 消息在链完成后才被处理 | 测试报告 + 计数器 |
| ACC-C-04 | 非阻塞 | JS 线程执行 500 ms 忙循环期间，另一线程调 `nexcore_send` × 1000 与 `nexcore_drain(0)` × 1000 | `send` p99 ≤ 1 ms、max ≤ 5 ms；`drain(0)` p99 ≤ 1 ms、max ≤ 5 ms；无调用阻塞超过 10 ms | 延迟直方图 |
| ACC-C-05 | 关闭 | 1000 个在途请求 + shutdown；其中 100 个注册**短** `.catch`（不触发硬上限）；另在 outbox 预置 100 条响应与 100 条事件、在 inbox 预置 100 条已接受未处理请求 | 所有 JS pending 以 `E_SHUTDOWN` 拒绝；100 个短 `.catch` 在 `shutdown` 返回前执行；outbox 响应/事件与 inbox 请求均不触发 sink，`NX_STAT_DROPPED_SHUTDOWN == 300`（100+100+100）；无回调在 free 后发生；ASAN 无 UAF | ASAN 报告 + 计数 |
| ACC-C-06 | 失控脚本 | `Promise.resolve().then(loop)` / `while(true){}` | 从触发到进入 FATAL ≤ 5 s；`drain` 首次返回 `NX_E_INTERNAL`；进程不崩溃；`shutdown/free` 后可重建并完成一次请求 | 测试报告 |
| ACC-C-07 | 重入防护 | sink 内 send、sink 内 shutdown、JS 内 emit、JS 内 free、Core 内 emit | send 成功；sink 内 shutdown 与 JS 内 free 返回 `NX_E_REENTRANT`（无 `thread_join(self)`、无死锁）；emit 不重入 | 测试报告 |
| ACC-C-08 | 条件变量超时 | 无消息时 drain(-1) 阻塞；shutdown 唤醒 | drain 在 shutdown 后 ≤ 100 ms 返回 `NX_E_SHUTDOWN` | 计时 |
| ACC-C-09 | 创建/关闭循环 | 1000 次 create→shutdown→free 循环；每组在 `shutdown` 后、`free` 前调一次 `send`/`drain` | 无泄漏、无崩溃；`shutdown` 后、`free` 前均返回 `NX_E_SHUTDOWN`；`free` 后不再调用（UB 不在验收范围）；ASAN 无 UAF/越界，LSAN 无泄漏 | ASAN/LSAN |
| ACC-C-11 | 关闭硬上限 | 构造 1000 个 `.catch`，其中一个在同步死循环里不返回 | `shutdown` 在**硬上限 700 ms（`shutdown_budget_ms` 500 + `shutdown_hard_increment_ms` 200）再加 `shutdown_schedule_slack_ms` 200 ms 调度余量**内返回 `NX_E_CLOSE_TIMEOUT`；记录 `NX_STAT_CLOSE_BUDGET_EXCEEDED`（500 ms 优雅预算耗尽）与 `NX_STAT_CLOSE_HARD_ABORT`（700 ms 硬中断）；不出现 `E_JS_RUNAWAY`、不进入 FATAL；无二次 settle；随后 `free` 成功 | 测试报告 + 计时 + 统计 |

## 5. D：事件

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-D-01 | 订阅/退订 | emit 前/中/后订阅；emit 中退订；重复退订 | 快照语义；退订后不再调用；正在执行的 handler 完成 | 测试报告 |
| ACC-D-02 | 保序 | 单 emitter 100k；多 emitter 交错 | 单 emitter 严格 FIFO；跨 emitter 不要求全序 | 测试报告 |
| ACC-D-03 | 背压 | 灌满 outbox 事件队列，同时发响应 | 事件按策略丢弃；每个“溢出→恢复”批次恰好一条带 `dropped` 的 `E_BACKPRESSURE`；响应 100% 不丢 | 测试报告 + 统计 |
| ACC-D-04 | 异常隔离 | 一个订阅者抛错，其他订阅者正常 | 其他订阅者仍收到；错误通过 `$error` 上报；emit 不失败 | 测试报告 |

## 6. E：代码生成

> 阶段说明：P1–P2 的生成器输入是 `src/units.json`（无 PyYAML），因此依赖 Schema YAML 的条目（E-03/E-05/E-06 等）在 **P3** 才可执行；当前阶段先跑 `units.json` 的确定性/漂移/符号表检查与 E-08 拓扑。判据不变，输入的 fixture 形态在 P3 补齐。另有一条**双源一致性**强制检查（不属于五条机械检查）：`tools/check.sh` ⑤b 解析每个 unit 的 `source` `.h.ct`，其**所有 `pub` 函数名**必须与该 unit 的 `methods` 集合完全相等；多出的 `pub` 函数视为漂移并失败（否则会静默漏掉映射），少报的 method 同样失败。⑤a/⑤b 已随 `tools/check.sh` 全量实跑通过（`SCRIPT_RC=0` + 逐项 PASS），并各有真失败负例。

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-E-01 | 确定性 | 同 Schema 连续生成两次，`diff -r` | 字节完全一致 | diff 输出 |
| ACC-E-02 | 漂移 | 手改任一生成物后 `nexcore-gen --check` | 退出码 2，打印首个差异 | CI 日志 |
| ACC-E-03 | Schema 负例 | 固定清单 `tests/schema-negative-v1.yaml` 共 20 条（未知类型、缺 `c_handler`、重复名、错误默认值、破坏性变更未升版本等，每条含输入片段与期望错误码） | 20 条全部拒绝，错误含文件:行:字段 | 负例报告 |
| ACC-E-04 | 无魔法规则 | 自动化部分：GEN-01..GEN-09、GEN-11..GEN-13、GEN-15..GEN-20、SYM-01..SYM-05；GEN-10/GEN-14 与“不得实现业务语义”为人工规则 | 自动化规则全通过；反例（宏生成调用链、跨单元取地址、手写方法名）全部失败；人工规则有签字 | 检查日志 + 评审记录 |
| ACC-E-05 | 四端覆盖 | 固定 `tests/fixture.schema.yaml`，统计每个方法在 C/Swift/Kotlin/JS 的声明与调用点（注册表字符串不计入声明） | 每端声明恰好 1 处、调用点 ≥1 处；无遗漏 | manifest + 脚本 |
| ACC-E-06 | 回调 wrapper | 所有 `&ident`/`= ident` 在本单元有定义 | 每个生成 `.c` 通过 `cc -Werror=implicit-function-declaration -fsyntax-only`；跨单元 `&f` 反例编译失败 | 编译日志 |
| ACC-E-07 | trait 传播 | 带 trait 结构体按值嵌套 | 生成器拒绝；指针嵌套也不允许进入 ABI | 负例日志 |
| ACC-E-08 | 单元拓扑（b′ 映射单元） | 固定 fixture `tests/topology/{root,a/a,b/b}`（目录=命名空间路径；A/B 均为命名空间单元，root 只作构建入口）；正例：A↔B 双向经 `import <ns>::<ns>_map` 与 `<ns>::<Trait>.<method>`，实现单元自身 `import a;` 属于合法且必需的声明可见性；负例：无映射时裸名/限定名/cname 三种写法、import 另一个实现单元、**实现单元删掉 self import**（CT 会 exit 0，必须由脚本拦下）、**在 `a_map.m.ct` 里 import `b`**（map 单元不得整类跳过）；再验证跨单元 `&f` | 正例编译链接运行通过；三类调用负例、“跨实现单元 import”、缺 self import、map 内部非法 import 均被判失败；跨单元 `&f` 仍失败，改用本地 wrapper 后通过；SYM-05 五条检查全过（③ 剥注释/字符串后无 `::`）；多 trait 同名负例按 `(ns, trait)` 消歧；⑤a/⑤b 已随 `check.sh` 全量实跑通过（`SCRIPT_RC=0` + 逐项 PASS），负例均确认真失败 | 编译日志 + CI 五条检查输出 |

## 7. F：AI 友好性

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-F-01 | 边界脚本 | `check-boundaries.sh` + `check-generated.sh` + `check-forbidden.sh` | 全部通过；违规样例能被拦截 | CI 日志 |
| ACC-F-02 | 框架扩展流程 | 用 `tests/fixture.schema.yaml` 新增一条 echo 方法：Schema→生成→stub `c_handler`→协议往返 | 不改 `src/`、`platform/`；≤5 步；四端生成；echo 往返成功；业务实现不在框架验收范围（HOST-03） | 流程记录 + diff |
| ACC-F-03 | 生成 API 完整性 | `grep -rn "nexcore_" js/app/`；检查每个方法的 JSDoc/doc 与类型 | 业务 JS 中直接出现 `nexcore_` 的次数为 0（生成绑定内部除外）；无缺失注释/类型 | 检查日志 + 生成物审查 |
| ACC-F-04 | AI 越界拦截 | 构造 5 个越界 PR（手改生成物、调未注册方法、空 catch、主线程 drain、跨层 include） | 其中至少 3 个由 CI 拦截，其余 ≤2 个由 review 拦截；无遗漏 | 演练记录 |
| ACC-F-05 | 边界门禁 | `grep -rn "quickjs.h" --include=*.ct`；`nm` 白名单；`grep -rn "pub .*nexcore_" src/` | `quickjs.h` 只出现在绑定单元；`nexcore_*` 只出现在根 ABI 单元；无平台框架头 | 检查日志 |

## 8. G：平台集成

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-G-01 | 框架集成骨架（macOS） | SwiftUI 最小宿主 + stub 业务：启动、UI→JS echo 往返、订阅一条事件、超时、取消、退出 | 6 条路径的**框架断言**：`send` 返回 0、sink 仅在 drain 线程、`E_TIMEOUT`/`E_CANCELLED` 正确、shutdown 后无回调；不判定业务结果；ASan 无报告 | 日志 + ASan；载体 SAMPLE-01 |
| ACC-G-02 | 框架集成骨架（Android） | Compose 最小宿主 + stub 业务：同上 + 进程重建 | 框架断言同 G-01；重建后 2 s 内首次请求成功、`schema_hash` 与重建前一致、连续 100 次往返成功、无泄漏；不判定业务状态恢复（HOST-02） | 日志 + 计数；载体 SAMPLE-02 |
| ACC-G-03 | 同一 Schema | 两端 `schema_hash`、JS bundle SHA-256 | 完全一致 | 启动日志 |
| ACC-G-04 | soak/泄漏 | 24h，1k req/s，含事件与超时；warmup 10 min 后取基线 | 无崩溃；RSS 增长 < 5% 且 < 8 MiB；Bridge 相关线程恒为 2（JS+drain）；fd 净增长 = 0（日志 fd 在白名单内） | 监控 CSV |
| ACC-G-05 | 性能 | 1 KiB payload，**并发度 1**（顺序往返），10k warmup + 100k 采样；另跑 32 并发吞吐测试 | 顺序：macOS p99 < 5 ms、Android p99 < 15 ms（暂定）；吞吐单独报告，不与延迟混算；JS 启动 < 100 ms | 基准报告（bench-v1） |
| ACC-G-06 | 生命周期顺序 | 记录 close 调用序列 | 严格“停 drain → shutdown → free”；`free` 竞争时只允许 `NX_E_BUSY` | 日志 |

## 9. H：安全与限额

| ID | 条目 | 方法 | 阈值/判定 | 证据 |
|---|---|---|---|---|
| ACC-H-01 | 限额与 fuzz | 固定种子 1M 次随机/畸形 JSON；超长字符串、深嵌套、坏 UTF-8、重复键 | 不崩溃；按协议返回错误；峰值 RSS ≤ 2× 基线，且结束后回落到基线 + 8 MiB 内 | fuzz 报告 + RSS 曲线 |
| ACC-H-02 | 无动态执行 | `grep` 生成 JS/业务 JS | 无 `eval`/`new Function`/`Proxy`/动态 `import` | 检查日志 |
| ACC-H-03 | 白名单与方向 | JS 调未注册方法；UI 调 core-only 方法 | `E_NO_METHOD` / `E_FORBIDDEN`；不进入 outbox | 测试报告 |
| ACC-H-04 | 脱敏 | 生产模式错误响应/日志；fixture 固定注入 `SECRET_DO_NOT_LOG_9f3a`、绝对路径 `/Users/alice/private/x.ct`、堆栈标记 `at handler (` | 三类字符串在响应与日志中出现次数均为 0；payload 只以长度/哈希形式出现 | 日志审查脚本 |
| ACC-H-05 | 资源上限 | 配置 `JS_SetMemoryLimit` 与 `JS_SetMaxStackSize`，分别跑分配炸弹与深递归 | 两者都由 QuickJS 抛 JS 异常；错误可观测（`drain` 返回 `NX_E_INTERNAL`，或收到 `$error` 的 `E_JS_RUNAWAY`），进程不崩溃、无 SIGSEGV；之后可 `shutdown/free` 并重建 | 测试报告 |

## 10. 性能测量协议

**基准版本：`bench-v1`**（定义见 [decisions.md](decisions.md) D-17）。任何协议、预算、负载定义变化都必须升版本。

- **环境**：Release 构建；记录设备身份（型号、芯片、RAM、OS build、编译器版本）；macOS 使用外接电源、关闭低电量模式；Android 飞行模式、关闭省电、固定亮度、记录 CPU governor。型号在 P4 前由 Q-16 固化；固化前只做同机纵向对比，不做跨机结论。
- **并发度**：延迟测试固定为 **1**（上一个响应到达后才发下一个）；吞吐单独用 32 并发测 60 s，单位 req/s。两者不得混在同一张表里。
- **负载**：1 KiB JSON，固定种子 `bench-v1-seed`，生成脚本与 payload SHA-256 一并归档；1 个 UI↔JS 往返；无事件洪水。
- **重复与离群**：同一配置至少 3 轮，报告各轮中位数与 IQR；**不剔除离群样本**，max 必须报告；不能用最好一轮代表整体。
- **采样与统计**：10k warmup，100k 测量；p50/p95/p99/max 与样本数全部报告；所有分位数用**同一种线性插值**定义（`idx = p*(n-1)`，取相邻两点线性插值），禁止只报均值或混用插值方式。
- **计时边界**：起点 `nexcore_send` 返回 0；终点 sink 收到响应。**包含** JS handler 与 Core handler 执行时间；另单独报告“不含 handler”的分段数据（从入队到出队、从出队到 sink），方法是把 handler 换成**立即返回常量的 stub**，并在测试构建中用单一 `now_ms` 打点；Release 基准不开启打点。
- **预算（暂定，P4 固化）**：
  - macOS 顺序 p99 < 5 ms；
  - Android 顺序 p99 < 15 ms；
  - JS 启动（bootstrap + app 脚本）< 100 ms；
  - 空载 shutdown < 50 ms；1000 pending 常态目标 < 500 ms，**硬上限 700 ms**（超时返回 `NX_E_CLOSE_TIMEOUT`，不视为性能通过）；
  - 32 并发吞吐只记录基线，不设通过阈值，除非 Q-16 给出产品目标。
- 任何预算变更必须写入 [decisions.md](decisions.md) 并升 `bench` 版本；不允许为了让测试通过而放宽。

## 11. 证据归档

```
build/reports/
  ACC-A-01.txt
  ACC-B-03.json
  ACC-C-05-asan.txt
  ACC-G-05-bench.csv
  ACC-H-01-fuzz.txt
```

每个条目至少一个文件；CI 将 `build/reports/` 作为 artifact 上传。评审只看 artifact 与命令，不接受口头结论。

## 12. 发布门槛

- P4 结束前，A/B/C/D/E/F/G/H 全部 `ACC-*` MUST 条目通过。
- `SAMPLE-*` 只作为 G 组的载体证据，**不作为框架发布的独立门槛**；样例业务是否完整不影响框架发布。
- `HOST-*` 由宿主项目自己的 CI 负责，**不进入框架发布门槛**。
- 未通过条目必须有：负责人、原因、影响、临时规避、关闭日期；P0/P1 不允许豁免。
- 发布 commit 必须记录：CT commit、QuickJS commit、schema hash、生成器版本、平台版本。
- 回滚方案：保留上一版 `libnexcore` + JS bundle + schema；回滚后 `schema_hash` 必须匹配，否则启动失败而不是带病运行。

## 13. SAMPLE：验证样例（不是框架交付物）

| ID | 条目 | 方法 | 判定 | 证据 |
|---|---|---|---|---|
| SAMPLE-01 | macOS 验证样例 | SwiftUI 最小 app，业务 core/JS 用 stub；覆盖启动、echo 往返、订阅、超时、取消、退出 | 能作为 ACC-G-01 的载体跑完 6 条路径；代码与截图仅用于验证框架，不随框架交付 | 仓库示例目录 + 日志 |
| SAMPLE-02 | Android 验证样例 | Compose 最小 app，业务用 stub；另测进程重建 | 能作为 ACC-G-02 的载体；重建路径可复现 | 仓库示例目录 + 日志 |

样例的定位：**证明框架能用**，不是产品第一版，也不是宿主项目的实现模板。样例中的业务 core、业务 JS、UI、Schema 都是替身。

## 14. HOST：宿主项目验收（不在框架范围）

| ID | 条目 | 归属 | 说明 |
|---|---|---|---|
| HOST-01 | 业务规则正确性 | 宿主 | 领域规则、边界值、错误分支；用宿主 core 的单元测试，不经过框架 |
| HOST-02 | 业务 JS 状态机/流程 | 宿主 | 编排顺序、状态迁移、缓存/降级策略；用宿主 JS 测试，只通过生成的 `nexcore` API |
| HOST-03 | 业务 Schema 内容与迁移 | 宿主 | 方法/事件/字段的业务含义、版本迁移策略；框架只校验形态与生成规则 |
| HOST-04 | UI 交互与视觉 | 宿主 | 布局、动效、无障碍、平台体验；框架只保证 transport 与绑定可用 |
