# NexCore 设计文档

ct-NexCore 是**跨端业务核心复用框架**：让宿主项目的业务 core 能在 macOS/iOS/Android 的原生 UI 上跑起来。框架本身**不是业务核心**，也不包含业务 JS、UI 或业务 Schema 内容；它只提供夹在宿主 UI 与宿主 JS 之间的桥接与生成能力。

框架交付物只有五类：

- QuickJS 运行时集成
- C ABI 桥接层
- JSON 协议
- 代码生成器 CLI
- 结构化调试日志

宿主项目负责：业务 core、业务 JS、原生 UI、业务 Schema 内容、业务测试。详见 [design.md](design.md) §1.3–§1.5。

本目录只描述**契约与边界**，不包含实现代码。实现代码、QuickJS 嵌入验证与 CT 能力核查分别由其他角色在各自目录推进。

- 状态：v0.9（`tools/check.sh` 由编码侧唯一维护，编号定为 `0 / ①a~①c / ②a~②c / ③ / ④ / ⑤a ⑤b / ⑥ / smoke` 并已全量实跑通过（`SCRIPT_RC=0`）；`build/abi`/`build/impl` 路径口径收敛到 design §6.1 一处；统计键按 D-20 冻结为 `1..18` + `19` 哨兵，5 个新键落 `10..14`，counter 采样随子系统落地）
- 适用范围：macOS/iOS（Swift）与 Android（Kotlin）为首批平台；Linux/Windows 暂不在首批验收范围
- CT 基线：`ct` 仓库 `main` 的 `c27da3d`（v0.19.1）。CT 迭代很快，本文引用的行为均标注了实测方式；升级 CT 后需重跑「CT 能力核查」中的探针

## 阅读顺序

| 顺序 | 文档 | 回答什么问题 | 主要读者 |
|---|---|---|---|
| 1 | [design.md](design.md) | 为什么这样分层、桥接层到底固定了什么、CT 能力边界与规避方式、ABI 与生命周期 | 所有人 |
| 2 | [protocol.md](protocol.md) | `{id, method, params}` / `{id, result\|error}` 的完整定义、订阅/发布语义、两个方向为何不对称 | 实现者、平台接入者、评审者 |
| 3 | [async-threading.md](async-threading.md) | 线程模型、队列与唤醒、Promise 与 pending map、微任务泵送时机、关闭与竞态 | 实现者、评审者 |
| 4 | [codegen.md](codegen.md) | Schema 形态、三类绑定与 C 注册代码、可逐行审阅的生成规则、变更传导 | 实现者、AI 编码者 |
| 5 | [ai-friendly.md](ai-friendly.md) | AI 可以改什么、不能改什么、如何用脚本和 CI 强制边界 | AI 编码者、评审者 |
| 6 | [roadmap.md](roadmap.md) | 四个实现阶段各自的可验证产出与退出条件 | 项目负责人、实现者 |
| 7 | [acceptance.md](acceptance.md) | 可判定的验收条目、测量方法与证据格式 | 评审者、测试者 |
| 8 | [decisions.md](decisions.md) | 关键决策的备选方案、取舍理由与重审条件 | 评审者 |
| 9 | [open-questions.md](open-questions.md) | 尚未拍板、会阻塞实现的问题 | 项目负责人 |

如果只有 15 分钟：读 [design.md](design.md) 的「固定模式」与「CT 能力核查」，再读 [open-questions.md](open-questions.md) 中标为 **P1/P2 阻塞** 的条目。

## 术语

| 术语 | 含义 |
|---|---|
| Framework | ct-NexCore 框架本身：QuickJS 集成 + Bridge + 协议 + 生成器 + 调试日志；**不含业务内容** |
| 宿主项目 | 使用框架的项目，提供业务 core、业务 JS、UI、Schema 内容与业务测试 |
| Core | **宿主侧**业务核心（CT/C 等实现），编译为库，通过生成的 registry 注册；不在框架仓库内 |
| Bridge | **框架**的桥接层：C ABI、线程与队列、协议分发、QuickJS 运行时宿主、生命周期 |
| JS 层 | **宿主侧**业务编排/胶水代码，运行在框架的 QuickJS 中，只能通过 `nexcore` 对象访问外界 |
| UI 层 | **宿主侧** SwiftUI / Compose 界面与平台能力，只能通过生成的绑定访问 JS |
| 信封 | 跨宿主 UI↔宿主 JS 边界的 JSON 对象：请求、响应、错误、通知 |
| turn | JS 线程处理一条入站消息、执行 JS、泵送微任务、处理超时的一个完整单元 |
| 固定模式 | Bridge 中不随业务变化、可逐条审查的结构性约束（见 design.md §4） |

## 编号约定

不同文档使用相同字母前缀，但含义按文档区分，引用时必须带文档名：

| 前缀 | 所在文档 | 含义 |
|---|---|---|
| `D-xx` | [decisions.md](decisions.md) | 决策记录（D-01..D-19） |
| `FIX-xx` | [design.md](design.md) | Bridge 固定模式约束（FIX-01..FIX-16） |
| `GAP-xx` | [design.md](design.md) | CT 能力缺口与规避（GAP-01..GAP-14） |
| `GEN-xx` | [codegen.md](codegen.md) | 生成器“无魔法、可逐行审阅”规则（GEN-01..GEN-20） |
| `SYM-xx` | [codegen.md](codegen.md) | 符号与单元拓扑规则（SYM-01..SYM-05） |
| `RC-xx` | [async-threading.md](async-threading.md) | 竞态用例（RC-01..RC-16） |
| `ACC-<组>-xx` | [acceptance.md](acceptance.md) | 验收条目，组为 A–H |
| `Q-xx` | [open-questions.md](open-questions.md) | 待确认问题 |

编号一经发布不复用；删掉的条目在文档中保留说明（例如 `R-01..R-03` 已被 `SYM-01..SYM-05` 取代）。

## 文档不包含

- 具体业务方法清单与业务 Schema 内容（由宿主项目定义；文档只给形态与约束）
- 业务 core、业务 JS、UI 的实现代码
- QuickJS 嵌入的验证代码与结论（由编码角色在实现目录产出）
- 平台 UI 的视觉与交互设计
- 任何 `src/`、`platform/` 下的实现代码

## 变更记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-11-06 | v0.1 | 首版：分层、协议、线程、代码生成、AI 边界、四阶段路线、验收与决策记录 |
| 2026-11-06 | v0.2 | 并入 CT 实测：trait/vptr 与数据形状、跨单元取址与裸调用静默错译、`ct build --emit-c` 唯一完整头、命名空间/import 规则；新增 R-01..R-03 与待确认清单 |
| 2026-11-06 | v0.4 | 定位纠偏：框架 ≠ 业务核心。重画三层归属（宿主 UI + 框架 + 宿主 JS/Core）、新增框架交付物/宿主职责/对宿主假设、验收分层（ACC/SAMPLE/HOST）、Demo 明确为验证样例、roadmap 阶段产出改为框架能力 |
| 2026-11-06 | v0.5 | 设计层修正：D-15 重开为 b′ 映射单元（`.m.ct @map` 实测双向成立，根转发保留备选；新增 GEN-20 映射生成、SYM-05 五条机械检查，重述 FIX-16/GAP-13/SYM-04/ACC-E-08）、`#ct-raw` 改为“实测可用但主动不采用”、关闭竞态 3a（join/DEAD 单点）与 3b（`stop_requested`/`abort_requested` 分离）、定位相关 ACC 改写（G-01/G-02/F-02/E-05）、`NexCoreConfig` MIN/CURRENT 与 Swift/Kotlin 时钟注入（D-18）、bench-v1 协议与 D-17 预算 |
| 2026-11-06 | v0.6 | 第三轮复核：关闭 turn 改为“优雅预算 500 ms + 硬上限 700 ms”，硬中断用 `NX_E_CLOSE_TIMEOUT`（不产生 `E_JS_RUNAWAY`）；`close_abort` 第三标志；3a 补 JS 崩溃、FATAL 收尾、JS 线程内 `free` 三条路径；b′ 补三处同步/多 trait 同名/map 误用/stale map 与头名冲突；D-15 论据只保留可校验性与 AI 友好度；失控场景改为场景表；ACC-C-11/A-04/A-08/A-11/C-03/E-08 与性能协议同步 |
| 2026-11-06 | v0.8 | 评审实测推翻前置声明的归因与处置：前置声明范围=**本次构建的单元集合**，改为**两趟构建**（Pass 1 只编 `NexCore.h.ct` 出唯一发布头，Pass 2 编实现）；`abi/expected-forward-decls.txt` 改为干净头断言（只允许 `struct NexCoreConfig;`），对外承诺恢复“只暴露 ABI 类型与函数”，已知限制改为“单趟会有、两趟不会”；检查 ⑤ 拆成 a) 实现 `.ct` 强制 self import（缺了 CT 静默 exit 0）、b) 白名单且 `*_map.m.ct` 内部同规则；新增 `units.json`↔`.h.ct` `pub` 函数集合一致性检查；统计键与返回码给出冻结数值表；更正 `pub typedef struct X X`（`unexpected token`）、同名 `.c`（两序 diff 为空）、枚举常量 `int` 不能 `sizeof` 三处理由 |
| 2026-11-06 | v0.7 | 编码反馈：明确 `ct build --emit-c` 会在 ABI 头写入内部 struct 前置声明（接受 + 注释 + golden，补“CT 无法只导出部分 struct”限制）；ABI 常量改用 `pub enum`（`#define` 不进导出头）；`NexCoreConfig` 只有 tag、无 typedef 别名；`.h.ct` 与 `.ct` 不自动配对（显式输入 + `nm` 断言，列为风险）；目录结构改为 `src/<ns>/<ns>.h.ct`（namespace 路径=目录路径）；import 三分（自身 `.h.ct` / `*_map.m.ct` / 禁止其他实现 `.ct`）；生成器输入暂改 `src/units.json`（P3 恢复 Schema YAML，二者并存）；transport 最小接口表（宿主只有 `now_ms`/`log` 两个可选回调）；关闭时间常量拆为 `pump_budget_ms`/`shutdown_budget_ms`/`shutdown_hard_increment_ms`/`shutdown_schedule_slack_ms` |
