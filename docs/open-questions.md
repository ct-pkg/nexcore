# 待确认问题清单

本清单只放**尚未拍板、且会影响契约或实现**的问题。每条给出：为什么卡、候选方案、本文档的临时默认、需要谁决定、最晚决策点。临时默认不是最终结论；未决策期间实现按临时默认推进，但必须在对应阶段退出前关闭。

优先级定义：

- **P1**：不决策就无法开始对应阶段的设计/编码。
- **P2**：可以按默认实现，但决策会改变公开契约。
- **P3**：可以后置，影响优化或范围。

状态：`OPEN`（未决策）、`DEFAULTED`（按临时默认推进）、`CLOSED`（已并入 decisions.md）。

| ID | 问题 | 优先级 | 状态 | 最晚决策点 | 决策人 |
|---|---|---|---|---|---|
| Q-01 | QuickJS 选 Bellard 还是 quickjs-ng，锁定哪个版本 | P1 | OPEN | P2 编码前 | 技术负责人 |
| Q-02 | 是否允许 JS 热更新（影响 iOS 合规） | P1 | CLOSED | 已关闭 | 产品 + 法务 |
| Q-03 | `int64`/金额在 JSON 中的 wire 格式 | P1 | CLOSED | 已关闭 | 技术负责人 |
| Q-04 | 大二进制通道 | P2 | DEFAULTED | P3 Schema 冻结前 | 技术负责人 |
| Q-05 | 生成器语言与依赖策略 | P2 | DEFAULTED | P3 编码前 | 技术负责人 |
| Q-06 | Swift/Android 集成形态与最低系统版本 | P1 | OPEN | **P1 出口前** | 平台负责人 |
| Q-07 | Android ABI 矩阵；Kotlin/Native 是否纳入 | P2 | DEFAULTED | P4 验收前 | 平台负责人 |
| Q-08 | Linux/Windows 是否在 v1 范围内 | P3 | DEFAULTED | P4 范围评审 | 项目负责人 |
| Q-09 | 事件投递保证的对外承诺 | P1 | DEFAULTED | P2 协议冻结前 | 技术负责人 |
| Q-10 | 默认超时与微任务预算的具体数值 | P2 | DEFAULTED | P2 退出前 | 技术负责人 |
| Q-11 | 多窗口/多实例是否支持 | P2 | DEFAULTED | P3 前 | 产品 + 技术 |
| Q-12 | 后台/挂起时的 JS 生命周期 | P1 | CLOSED | 已关闭 | 平台负责人 |
| Q-13 | JS 信任模型与沙箱强度 | P1 | CLOSED | 已关闭 | 安全 + 技术 |
| Q-14 | Schema 版本兼容与 `schema_hash` 策略 | P1 | CLOSED | 已关闭 | 技术负责人 |
| Q-15 | 日志、遥测与 PII 边界 | P2 | OPEN | P4 发版前 | 安全 + 产品 |
| Q-16 | 性能参考设备与最终阈值 | P2 | OPEN | P4 基准前 | 项目负责人 |
| Q-17 | JS 模块拆分与打包方式 | P2 | DEFAULTED | P3 编码前 | 技术负责人 |
| Q-18 | 是否允许 UI 对 Core 的 fast path | P3 | DEFAULTED | v1 后重审 | 技术负责人 |
| Q-19 | 错误码归属、扩展与本地化 | P2 | DEFAULTED | P2 协议冻结前 | 产品 + 技术 |
| Q-20 | CT 版本 pin 与升级策略 | P1 | DEFAULTED | P1 编码前 | 技术负责人 |

---

## Q-01：QuickJS fork 与版本

**为什么卡**：Bellard QuickJS 与 quickjs-ng 在 `JS_NewClassID` 签名、构建系统、部分 API（如 `JS_NewCFunction` 宏、Atom 相关接口）上不同。绑定层一旦按某个 fork 写死，换 fork 要改 shim 与所有调用点。

**候选**：

| 方案 | 优点 | 代价 |
|---|---|---|
| A. Bellard QuickJS（固定 release tag） | 最接近原始 API；资料多 | 维护频率低；部分平台补丁需自打 |
| B. quickjs-ng（固定 commit） | 活跃维护；CMake 友好；bug 修复多 | API 有差异；社区相对小 |
| C. 自维护 fork | 完全可控 | 长期维护成本高 |

**临时默认**：A，固定 tag + 提交 hash；所有 API 差异只出现在 `src/js_host/js_host_map.shim.c`。

**决策依据**：P2 第一周做双 fork 的最小嵌入对比（编译、`JS_ExecutePendingJob`、内存限制、interrupt handler、Android NDK 构建），以实测结果定。

**关闭动作**：在 decisions.md 增加 D-20，记录 fork、commit、构建参数、许可证文件路径。

---

## Q-02：JS 热更新与 iOS 合规

**为什么卡**：决定 JS 是随包发布还是可从网络下载执行，直接改变启动流程、缓存、签名校验、回滚与安全边界；也决定是否需要额外的 bundle 管理 ABI。

**候选**：

| 方案 | 优点 | 代价 |
|---|---|---|
| A. 仅随包发布 | 合规风险最低；无下载/校验链路 | 修 JS bug 必须发版 |
| B. 下载 JS bundle，但只执行白名单签名包 | 可快速修复编排逻辑 | 需签名、版本、回滚、灰度；iOS 审核需逐案确认 |
| C. 服务端下发配置而非代码 | 合规风险低 | 表达力有限 |

**已关闭（选择 A）**：v1 的 JS **只随 App 包内置，不从服务端下载执行**。这是基于 Apple App Review 2.5.2 的合规约束，不是技术限制；`bootstrap_js` 由平台从包内读取。D-11 与 design §9 已按 A 固化。

**若将来要做下发**：必须单独评审，新增“bundle 来源、签名、校验失败、回滚、离线”五节契约，并升协议/ABI 版本。

---

## Q-03：`int64` 与金额的 wire 格式

**为什么卡**：JSON number 只有 IEEE-754 双精度安全整数 ±(2^53-1)。ID、时间戳、金额都可能越界。三家 JSON 实现（QuickJS、Swift `JSONDecoder`、Kotlin `kotlinx.serialization`）对超界数字的失败行为不一致。

**候选**：

| 方案 | 优点 | 代价 |
|---|---|---|
| A. `int64` 一律十进制字符串 | 无精度损失；三端一致 | 调用方需转换；Schema 要标注 |
| B. number，超出 ±2^53-1 报错 | 简单 | 合法业务值被拒 |
| C. number + 字符串双写 | 兼容 | 协议冗余、易不一致 |

**已关闭（选择 B）**：`int64` 用 number，超出 ±(2^53-1) 返回 `E_INVALID_PARAMS`；金额用最小货币单位，避免小数。与 protocol.md §3.3、codegen.md §2.3 一致。若将来出现合法大整数 ID，再单独立项改为十进制字符串（A）。

**影响面**：Schema 类型映射（codegen.md §2.3）、协议字段校验（protocol.md §3.3）、AI 生成代码的转换模板。

---

## Q-04：大二进制通道

**为什么卡**：1 MiB 限额与 Base64 膨胀（约 33%）决定图片/文件/音视频是否根本不能走本协议。

**候选**：

| 方案 | 优点 | 代价 |
|---|---|---|
| A. 只走 Base64，超限报 `E_PAYLOAD_TOO_LARGE` | 协议单一 | 大文件不可用；内存峰值高 |
| B. 增加分片方法（`$blob.open/chunk/close`） | 复用现有协议 | 需要句柄生命周期与超时 |
| C. 平台侧独立文件通道，协议只传路径/句柄 | 性能最好 | 破坏“唯一协议入口”；跨端语义不同 |

**临时默认**：v1 用 A；>64 KiB 明确不建议。若产品有大文件需求，P3 前定 B。

**关闭动作**：定 B 时补充分片状态机、并发、取消、超时、最大总量与清理规则。

---

## Q-05：生成器语言与依赖

**为什么卡**：生成器是开发期工具，但决定 CI 环境、贡献门槛与“生成物可审计”的可行性。

**候选**：Python 3 + PyYAML（D-06 现行）、CT 自举、自写 YAML 子集、Node/TypeScript。

**临时默认**：P1–P2 用 `src/units.json`（Python 标准库 `json`，无 PyYAML）；P3 接入业务 Schema 后回到 Python 3 + 锁定版本 PyYAML，同时保留 `units.json` 作为单元/符号清单。生成器不联网、不读环境变量、输出确定。

**待确认**：CI 是否允许安装 Python 依赖；是否要求离线可构建（影响是否 vendor wheel）。

---

## Q-06：Swift/Android 集成形态与最低版本

**为什么卡**：决定 transport 的实现边界与验收设备。

**需要确认**：

- Swift：SPM system library + modulemap 还是 XCFramework？最低 iOS/macOS 版本？
- Android：AAR 分发还是源码模块？JNI 由 `libnexcore.so` 直接导出，还是额外一层 `libnexcore_jni.so`？
- 是否要求 Swift 6 严格并发（影响 `@MainActor` 与 `Sendable` 标注）。

**临时默认**：Swift 用 SPM system library；iOS 15+/macOS 12+；Android minSdk 26、JNI 层独立 `.so`、Kotlin 协程封装。

**风险**：Swift 6 严格并发会让 `NexCoreSink` 的 C 回调标注复杂化，需在 P4 前确认。

---

## Q-07：Android ABI 矩阵与 Kotlin/Native

**为什么卡**：ABI 越多，NDK 构建与包体越大；Kotlin/Native 的 C interop 未实测，若纳入会改变生成物形态（`cinterop` def 文件 vs JNI）。

**临时默认**：只验收 `arm64-v8a` 与 `x86_64`（模拟器）；Kotlin/Native 不在 v1 验收范围（codegen.md §5.2、acceptance ACC-A-06/ACC-A-07）。

**待确认**：是否要求 `armeabi-v7a`；是否要求 iOS 侧同时支持 Kotlin Multiplatform 调用。

---

## Q-08：Linux/Windows 范围

**为什么卡**：线程 shim、动态库导出、路径与编码都不同。若 v1 承诺桌面，需要额外的 CI 与验收条目。

**临时默认**：不在首批验收范围；shim 预留 POSIX/Windows 分支位置，但不实现、不测试。

---

## Q-09：事件投递保证

**为什么卡**：`reliable` / `latest` 的对外语义必须写进契约，否则业务方会假设 at-least-once。

**临时默认**（D-07）：容量内保序、不重、不丢；容量溢出时 `reliable` 事件丢弃并上报 `E_BACKPRESSURE`，`latest` 只保留最后一个。**不承诺** at-least-once、崩溃恢复、跨进程。

**待确认**：是否存在“绝不能丢”的业务事件；若有，必须改为请求/响应 + 幂等键 + 本地持久化，而不是加强事件通道。

---

## Q-10：默认超时与微任务预算

**为什么卡**：数值进入 `NexCoreConfig` 默认值与验收阈值；太松掩盖问题，太紧误杀正常业务。

**临时默认**：请求默认 5000 ms；`nexcore_drain` 无限等待仅在 transport 线程；`MAX_JOBS_PER_TURN = 10,000` 只是让出配额（达到后开 continuation turn 继续泵，不是失败）；失败条件是连续泵送窗口 > 200 ms 或单脚本 > 50 ms，触发 `E_JS_RUNAWAY` 并 fatal shutdown（D-05）。

**待确认**：是否有已知的长任务（大列表处理、加密、批量导入）需要单独预算或后台任务机制。若有，需要新增“长任务”协议而不是放宽全局预算。

---

## Q-11：多窗口/多实例

**为什么卡**：一个 `NexCoreHandle` 对应一个 JS runtime。多窗口若共享 handle，需要 JS 侧会话隔离；若每窗口一个 handle，需要多个 JS 线程与独立 schema 校验。

**临时默认**：v1 一个进程一个 handle；多窗口共享同一 JS 层，通过 `scope`/`session` 字段区分（Schema 预留，协议 v1 不启用）。

**待确认**：iPad 多窗口、Android 分屏是否属于首发场景。

---

## Q-12：后台/挂起时的 JS 生命周期

**为什么卡**：iOS 进入后台后线程可能被挂起；Android 进程可能被回收。若 JS 线程持有锁被挂起，drain 线程可能阻塞。

**候选**：

| 方案 | 优点 | 代价 |
|---|---|---|
| A. 后台继续运行 | 逻辑简单 | 耗电、被系统杀死风险 |
| B. 后台暂停 JS turn，仅保留队列 | 省电 | 恢复时需处理积压与超时 |
| C. 后台 shutdown，前台重建 | 最省资源 | 状态需持久化；重建成本 |

**已关闭（选择 B-lite，不新增 ABI）**：v1 不提供 `nexcore_pause`。平台在进入后台时停止调用 `nexcore_drain` 并停止发送；在途请求按各自超时结束（假时钟可用于测试）。恢复后先 `drain(0)` 清空积压，再恢复正常投递。JS 线程本身不暂停，避免“持锁被挂起”的死锁。

**代价**：后台期间 JS 仍可能执行已入队 turn，产生少量 CPU；若实测耗电不可接受，再评估 ABI v2 的 pause/resume。

---

## Q-13：JS 信任模型与沙箱强度

**为什么卡**：决定是否允许第三方/运营侧下发 JS、是否必须做字节码校验、是否限制 `JS_SetMemoryLimit` 之外的系统调用。

**已关闭（选择同信任级）**：JS 随包发布、由内部编写，与 App 同信任级；沙箱只做能力收敛（无文件/网络/计时器），不做对抗性隔离。Q-02 已关闭热更新，因此不存在第三方脚本场景。

**前提变化时**：若将来允许下发脚本，本结论立即失效，必须重新评审沙箱与 Q-02。

---

## Q-14：Schema 版本兼容与 `schema_hash`

**为什么卡**：当前设计是三端 hash 必须一致，否则启动失败。这适合同版本发布，但不支持“旧客户端 + 新 JS”或灰度。

**候选**：

| 方案 | 优点 | 代价 |
|---|---|---|
| A. 严格相等 | 简单、无兼容负担 | 不能灰度、不能独立更新 JS |
| B. 旧 schema 是新 schema 的子集即可启动 | 支持增量 | 需要生成器产出能力集，运行时比对 |
| C. 方法级版本协商 | 最灵活 | 协议与生成器复杂度大幅上升 |

**已关闭（选择 A）**：v1 三端 `schema_hash` 严格相等；不匹配返回 `NX_E_SCHEMA_MISMATCH`。`schema_hash` 保持单字符串，字段形状冻结在 ABI v1（D-16）。由于 JS 随包内置（Q-02），不存在“旧客户端 + 新 JS”的合法组合。

**若将来要求 JS 独立发版**：选 B，生成器输出 `schema_capabilities`（方法名 + 版本），并升 ABI 版本。

---

## Q-15：日志、遥测与 PII

**为什么卡**：错误 `message`/`data`、协议 payload、JS 异常堆栈都可能含用户数据；日志脱敏规则必须可检查。

**需要确认**：

- 哪些字段允许进日志；是否允许记录完整 payload（debug 构建）。
- 崩溃上报是否包含 JS 堆栈与源码片段。
- 埋点由 JS 层还是平台层负责（当前设计：JS 只发事件，平台决定上报）。

**临时默认**：生产构建不记录 payload 与堆栈；错误只记录 `code`、`method`、`id`、耗时；debug 构建由 `flags` bit0 控制。

---

## Q-16：性能参考设备与阈值

**为什么卡**：acceptance ACC-G-05 的 p99 阈值是暂定值，没有设备就无法判定；`bench-v1` 的定义已固化（D-17）。

**临时默认**：macOS Apple silicon（具体型号待定）；Android 中端机 API 34 arm64（具体型号待定）。阈值见 acceptance.md §10，P4 首次基准后固化。

**待确认**：是否以最低支持机型作为基准（更保守），还是以主力机型（更宽松）。

---

## Q-17：JS 模块拆分与打包

**为什么卡**：v1 不做模块加载器，只有 `bootstrap_js` 一段源码。业务代码增长后需要拆分与打包。

**临时默认**：构建期用打包工具把多文件合成单段源码，运行时无 `import()`、无模块系统；生成器输出的命名空间挂到 `globalThis.nexcore`。

**待确认**：是否允许打包工具引入 polyfill/helper（会破坏“无魔法”审查）；建议禁止，保持生成物可逐行审阅。

---

## Q-18：UI 对 Core 的 fast path

**为什么卡**：当前所有 UI 调用都经过 JS（D-13）。若启动路径或高频路径性能不达标，可能需要受控直连。

**临时默认**：v1 不做。若做，必须满足：只读、无状态、单独 ABI 函数、单独的验收与安全评审，且不改变默认入口。

---

## Q-19：错误码归属、扩展与本地化

**为什么卡**：错误码由 Bridge、Core、JS 三方产生；若没有归属规则，会出现同码不同义。

**临时默认**：

- Bridge 产生 `E_*` 传输/协议错误；Core 产生领域错误码（`E_DOMAIN_*`）；JS 只能透传或包装，不得新造与 Bridge 同名的码。
- `code` 稳定、只增不改；`message` 不稳定、不参与断言；本地化在 UI 层按 `code` 映射，不在协议里传多语言文案。

**待确认**：领域错误码是否也由 Schema 定义（建议是，便于生成三端常量）。

---

## Q-20：CT 版本 pin 与升级策略

**为什么卡**：CT 迭代快，`c27da3d` 的实测结论可能在新版本变化；升级会牵动 ABI 头、`pub` 导出、trait 布局与 `ct build` 行为。

**临时默认**：

- 固定 `c27da3d`（v0.19.1）作为 v1 基线；CI 使用固定 commit。
- 每次升级必须重跑 design §5 的全部探针，并更新 `docs/` 中的实测结论。
- 升级 PR 必须包含：探针结果 diff、生成物 diff、ABI 头 diff、三端编译结果。

**待确认**：是否允许在 P1–P4 期间升级；建议只在阶段边界升级。
