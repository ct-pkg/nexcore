# 提案修订清单：Shell 宿主提案（17 条必改表述）

> 用途：交给提案方修订用。**本文件不改提案原文**，只逐条给出「原表述 / 为什么不成立 / 建议改法」。
> 上游：[nexcore-shell-host-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md)（§8 的展开版）、[PH-10 卡](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md)
> 基线：`2aec863` 之后、`98fdfdd` 之前的工作树快照（实测见裁定文档 §2、§3）
> 定位约定：提案未被跟踪，故按「条目位置」引用（如「一、2 已完成 第 3 条」）。

---

## 0. 三条**不必改**的表述（先划清，避免过度修订）

| 提案位置 | 原表述 | 为什么站得住 |
| --- | --- | --- |
| 一、3 | 「Promise pending、任务队列、超时/取消仅存在于设计/experiments，不在 `src`」 | 实测成立：`src/` 零命中，全部在 `experiments/src/bridge.ct:108/131/147/170/232` |
| 四、4.2 | 「Linux 内核 shebang 仅支持单个参数」 | 成立，是内核行为，不是本仓库事实 |
| 四、4.3 | 「脚本拥有等价 shell 执行权限，默认模式开放」 | 成立；但需要补发布口径（见第 16 条） |

---

## 1. 逐条清单

| # | 位置 | 原表述 | 为什么不成立 | 建议改法 |
| --- | --- | --- | --- | --- |
| 1 | 一、2「已完成」第 2 条 | 「同步调用链路、消息信封协议、队列、配置结构体版本协商（`struct_size`）」 | `protocol` 单元**已编译但从未被 `NexCore.ct` 调用**：`grep -n protocol src/NexCore.ct` 只有一处 `protocol_version` 比较（[NexCore.ct:73](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L73)）；`send` 把消息原样入队，`pump_inbox` 直接 `eval(msg)`（[:182](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L182)），没有任何 method 分发 | 拆成两条：「队列 + 配置版本协商：已落地」；「信封协议：只有结构体定义（[protocol.h.ct:19-22](file:///Users/samxx/trae-ws/nexcore/src/protocol/protocol.h.ct#L19-L22)），未接线，`send` 当前按表达式求值」 |
| 2 | 一、2「已完成」第 3 条 | 「原生 Provider 注册机制：宿主可以注入 JS 可调用的原生能力」 | `src/` 与公开 ABI 均无注册入口：ABI 只有 7 个函数（[NexCore.h.ct:113-125](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L113-L125)），`register_native()` 只在探针（[bridge.ct:240-248](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L240-L248)） | 整条从「已完成」移到「尚未落地」，并注明「有探针、无产品入口；PH-10 正在补」 |
| 3 | 一、1 | 「对外 ABI 为纯 C，宿主无关」 | 前半句成立，但不足以支撑后续方案推断：当前 ABI 既无脚本入口、无错误面、也无 Provider 注册 | 保留原句，追加一句「当前 7 个函数：`create/set_sink/send/drain/shutdown/free/stat`；脚本执行、错误文案、原生注册均未在 ABI 内」 |
| 4 | 一、4 | 「P1 阶段关键未决决策（阻塞项）… QuickJS 依赖裁定 … 需要正式裁定」 | 已裁定并已实施：[decisions.md D-21](file:///Users/samxx/trae-ws/nexcore/docs/decisions.md#L402-L412)；提交 `834736a`（改可选后端）、`4b943fd`（接通 quickjs 档）；两档实测绿 | 改为「QuickJS 依赖已裁定（D-21）：P1 不引入，P2 起作为 `js_backend` 的实现；本提案按该裁定执行，无待裁项」 |
| 5 | 四、4.1 第 3 条 | 「需要先完成 …`docs/reviews/nexcore-p1-quickjs-battle.md` 的裁定」 | 该文件第 2 行自述「**本文只放两席材料，不含裁定**」（[battle:2](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-p1-quickjs-battle.md#L2)）；裁定在 `nexcore-p1-quickjs-ruling.md`（状态行「已裁定」） | 改引 `docs/reviews/nexcore-p1-quickjs-ruling.md`，并把「待裁定」改为「已裁定，见 D-21」 |
| 6 | 二、2.1 第 3 条 | 「复用现有 `libnexcore` 全部能力：信封协议、RPC 调用、原生 Provider 注册、内存配额配置」 | 四项里只有「内存配额配置」与「队列」在库里；信封、RPC、Provider 注册三项不在（见第 1、2 条） | 改为「复用：handle 生命周期、队列、`struct_size` 协商、`stat`；信封/RPC/Provider 注册待库层补齐（PH-10 / PH-11）」 |
| 7 | 二、2.2 方案 A | 「链接预编译完成的 `libnexcore.a`」 | 仓库**没有** `.a` 产物：`check.sh` 只产 `build/bin/libnexcore.dylib`（[check.sh:454-456](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L454-L456)），`ls build/bin` 无 `.a`；`.gitignore` 还忽略 `*.a` | 改为「链接 `build/bin/libnexcore.dylib`（当前唯一产物）；如需 `libnexcore.a`，作为独立构建任务（PH-10 的 D3）」 |
| 8 | 三、阶段 1 交付物 1 第 3 点 | 「执行顶层 JS 代码，泵送微任务 `js_std_loop`」 | 两处都不成立：①`js_std_loop(JSContext*)` 需要 ctx，ABI 只给不透明 `NexCoreHandle`，宿主取不到；②NexCore 的求值是**表达式**模式（源码被包成 `(src)`，[js_backend_quickjs.ct:87-99](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L87-L99)），多语句脚本实测无输出；③泵应放在 `js_backend` 内，而不是宿主调 libc 辅助函数 | 改为「脚本执行需库层提供程序模式求值入口；微任务由库在求值后泵空，宿主入口是 `send`+`drain`（PH-10 D1）」 |
| 9 | 三、阶段 1 交付物 2 | 「JS 注入 API（仅宿主内）`Shell.print` / `Shell.execSync`」 | ABI 无注册入口（第 2 条），且 `console` 都不存在：实测 `send "console.log('hi')"` → `count=0`（无异常透出） | 加前置：「依赖库层新增原生注册入口（PH-10 D2）；在此之前宿主无法注入任何能力」 |
| 10 | 三、阶段 1 交付物 3 | 「脚本异常打印堆栈，返回合理进程 exit code」 | 异常文案在库内被销毁：`JS_GetException` 取到即 free、只返回 `-1`（[js_backend_quickjs.ct:101-105](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L101-L105)）；`NexCore.ct` 只写 `last_error=NX_E_INTERNAL`，不投递（[:182-188](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L182-L188)） | 改为「错误面需库层透出（code + message 出参）；当前只能拿到 `stat(LAST_ERROR)` 的一个整数，无堆栈」 |
| 11 | 三、阶段 1 风险提示 | 「阶段 1 全部使用已落地的库能力，不依赖 experiments 内未合并的异步代码，可以快速验证」 | 反了：它依赖三样**连探针都没有**的库能力（脚本入口、错误面、注册入口），比 `experiments` 里的 pending 更靠前 | 整句替换为「阶段 1 的主要工作量在库层（三项新增契约），宿主侧是薄胶水；在库层就位前不具备开工条件（入口条件见 PH-10 卡 §10）」 |
| 12 | 五、【重要】1 | 「阶段 1 MVP，对 `src` 库代码零修改」 | 与第 8/9/10 条互斥：三条交付物都要动 `src`。另实测「表达式模式」连 `var a=1; a+1` 都跑不了 | 改为「阶段 1 允许并必须修改 `src/js_backend/`（程序模式求值 + 错误面）；`NexCore.ct` 与 ABI 函数数不动」 |
| 13 | 五、4 | 「阶段 3：补充测试用例并入 `tools/check.sh` 门禁」 | `check.sh` 明写归编码侧唯一维护、需求侧只提检查项（[check.sh:5-12](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L5-L12)）；且让库门禁依赖「宿主可构建」会把 P1–P4 的绿线绑到宿主上 | 改为「宿主用例走独立脚本 `tools/check-host.sh`；若需进总门禁，只由编码侧在总入口末尾单点调用」 |
| 14 | 四、4.4 第 1 条 | 「编译器版本 v0.44.1 约束」 | 仓库基线是 `ct v0.44.2`（`328e5e63`，[README.md:19](file:///Users/samxx/trae-ws/nexcore/docs/README.md#L19)）；PATH 上的 v0.44.1 是系统旧版、非基线 | 改为「`ct v0.44.2`（`328e5e63`）；v0.44.1 只是本机系统的旧二进制，不作为基线」 |
| 15 | 四、4.4 第 1 条（同上） | 「需要处理 CT 单元、units.json、外部 C 头导入、编译器版本约束，维护成本更高」 | 作为「选 A」的理由不成立：实测 `ct v0.44.2` 一次 `ct build` 编 8 个单元出可执行（`main` + 7 单元，rc=0）；CT 写库内单元 + `#include` 外部头也已在用（[js_backend_quickjs.ct:4](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L4)） | 删掉这条理由。若仍选 A，改用三条：①C 宿主被迫只走公开 ABI（当 ABI 消费者）；②边界门禁禁 `#if`/`errno`，与 shell 代码天然冲突；③沿用 `tests/*.c` 范式 |
| 16 | 四、4.3 第 1 条 | 「沙箱模式必须显式开启；默认模式下 `Shell.execSync`/`Shell.spawn` 完全开放」 | 策略本身站得住，但缺两条落地口径，会被读成「可分发工具」 | 追加：「`nex` 不作为发布物、不安装到系统目录；文档默认写 `#!/usr/bin/env nex`（`#!/bin/nex` 需 root）；默认模式等同任意本地代码执行，只用于开发者本人脚本」 |
| 17 | 六、2 | 「实现阶段 1 MVP（C 宿主）：新增 `hosts/shell` 目录 … 不触碰库层」 | 前提不成立（第 11/12 条）：宿主能做的只有 argv/读文件/调用 ABI，`Shell.*` 与脚本执行都得先动库 | 改为「先完成库层四项契约（PH-10），再新增 `hosts/shell/`；宿主阶段对 `src/` 零修改」 |

---

## 2. 修订后的建议结构（只给骨架，不代写）

1. **现状**：按 `src/` 实际能力写，分「已落地 / 有探针无入口 / 无」，每行带文件:行号。
2. **裁定引用**：D-21 与 `nexcore-p1-quickjs-ruling.md`，不再表述为待裁。
3. **分期**：PH-10（库层四项）→ 宿主 MVP → 实用 → 生产；每期写清「哪部分属库层」。
4. **方案选择**：保留 A，换理由；把 B 写成「已实测可行但当前不选」，不写「更难/不可行」。
5. **安全与发布**：默认开放 + 不作为发布物 + `env nex`。
6. **开放问题**：四条按裁定文档 §7 的结论直接写成决定项，不再作为待讨论。

---

## 3. 使用注意

- 本清单只覆盖「与仓库事实冲突」的表述；**不建议**改动的三条见 §0。
- 第 4/5/14 条属「过期信息」，改后不影响方案；第 2/6/8/9/10/11/12/17 条属「前提错误」，不改则阶段划分不成立。
- 若提案方对某条有异议，先给可核对的文件与行号或一段实测，再回到裁定；不接受用「惯例/经验」替代证据。
