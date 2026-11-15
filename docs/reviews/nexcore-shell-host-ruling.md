# 裁定：独立 Shell 宿主（`nex` 解释器 / shebang）提案评估

> 裁定日期：2026-11-15 ｜ 评审对象：外来提案《增加独立 Shell 宿主（nex 解释器 / shebang `#!/bin/nex`）》（以下简称「提案」，未被跟踪，故按条目位置引用）
> 核实基线：测量时 `HEAD = 2aec863` + 工作区未提交的 P3 改动；撰写期间该改动已提交为 `98fdfdd`（`src/NexCore.ct` 内容与测量快照逐字节相同，`tools/check.sh` 其后又新增 ⑪–⑭ 共 1 行头部注释与检查段，本文引用的 `check.sh` 行号已按 `98fdfdd` 校正）
> 方法：先核四个前提，再跑三项实测，再加三项提案未要求但改变结论的实测；之后主张席 / 反证席各一轮，最后裁定
> 约束遵循：只读仓库 + 只写本文件；未改 `src/`、`tools/`、`docs/roadmap.md`、`.ai-engineering`、验收条目；未 push、未打 tag

---

## 0. 结论先写

| 对象 | 判定 | 一句话理由 |
| --- | --- | --- |
| 方向（给本地脚本调试提供一个 CLI 宿主） | **GO** | 与 P4「接入契约 + 验证样例」同向，且不污染库定位 |
| 提案的**阶段 1**（C 宿主、对 `src/` 零修改、泵 `js_std_loop`、注入 `Shell.print/execSync`） | **NO-GO** | 五条交付物里有三条在今天的库上**根本不存在**，不是「已落地能力」 |
| 阶段 1 的**前提句**「不依赖任何未落地库特性」 | **不成立** | 见 §3.1 / §3.2：无 Provider 注册入口、无脚本入口、异常文案被丢弃 |
| 方案 A（纯 C 写宿主 main） | **采纳** | 但理由要换（不是「CT 写可执行维护成本高」），见 §5 |
| 方案 B（CT 写宿主） | **保留为备选，且已实测可行** | 8 单元 `ct build` 产可执行实测通过（§4.6） |
| 三阶段划分 | **要改：插入阶段 0** | 库层契约先动，宿主才有可调用的东西 |
| 「QuickJS 依赖 P1 未决」 | **过期** | 已由 D-21 裁定并已实施（`834736a`、`4b943fd`） |
| 「需先完成 `docs/reviews/nexcore-p1-quickjs-battle.md` 的裁定」 | **引用错位** | 该文件明确「不含裁定」；裁定在 `nexcore-p1-quickjs-ruling.md` |

---

## 1. 四个前提核实

### 前提 1：QuickJS 依赖「P1 未决」——**过期，已裁定**

提案原文：「P1 阶段关键未决决策（阻塞项）… 需要正式裁定」「需要先完成 P1 未决决策文档…的裁定」。

仓库事实：

- 裁定已存在：[decisions.md](file:///Users/samxx/trae-ws/nexcore/docs/decisions.md#L402-L412) 的 **D-21**：「P1 不引入 QuickJS 依赖。JS 调用改经 P1 自有的 `js_backend` 接口单元（空实现），QuickJS 作为 P2 的后端实现后移；`tools/check.sh` 增设 `NEXCORE_JS=null|quickjs` 两档。」
- 裁定正文与判据：[nexcore-p1-quickjs-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-p1-quickjs-ruling.md#L9-L18)，状态行「**已裁定**（2026-11-15）」。
- 已实施：`834736a`（按 D-21 把 JS 依赖改为可选后端）、`4b943fd`（接通 quickjs 档）；D-21 末段自己写了实施状态（[decisions.md:412](file:///Users/samxx/trae-ws/nexcore/docs/decisions.md#L412)）。
- 两档都实测绿：本机 `PATH=~/.local/ct/0.44.2:$PATH NEXCORE_JS=quickjs bash tools/check.sh` → `ALL CHECKS PASSED (NEXCORE_JS=quickjs)`，rc=0（§2.0）。

**判定**：提案这一条不仅过期，而且它把「阻塞项」写反了——今天的 `js_backend` 接口正是为了让宿主与引擎解耦才拆出来的，提案若在 D-21 之前写，结论还应是「等裁定」；今天则应改成「按 D-21 执行，宿主侧只需面对 `js_backend` 契约」。

### 前提 2：`docs/reviews/nexcore-p1-quickjs-battle.md` 是否存在——**存在，但它是两席材料不是裁定**

- 文件存在：[nexcore-p1-quickjs-battle.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-p1-quickjs-battle.md#L1-L5)，第 2 行原文：「本文只放两席材料，**不含裁定**」；盘点时间 HEAD `89de83d`。
- 真正的裁定文档是 [nexcore-p1-quickjs-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-p1-quickjs-ruling.md#L1-L5)，它在第 4 行把自己的上游写成 battle 文档：「上游材料：…（两席，未裁）；本文补完两席的空白项并给出判据」。

**判定**：提案「引用了一个存在的文件」这点站得住；「该文件待裁定」站不住。两份文档是「材料 → 裁定」的前后关系，不是同一份。

### 前提 3：基线 `808fc53` 与 HEAD 的距离，`src/` 动了什么——**10 个提交，`js_backend` 是全新单元，`js_host` 已标废弃**

```
$ git rev-list --count 808fc53..2aec863        # 测量时的 HEAD；98fdfdd 之后再 +1
10
$ git diff --stat 808fc53..2aec863 -- src/
 src/NexCore.ct                         |  16 ++--
 src/js_backend/js_backend.ct           |  54 +++++++++++++
 src/js_backend/js_backend.h.ct         |  20 +++++
 src/js_backend/js_backend_map.m.ct     |  20 +++++
 src/js_backend/js_backend_quickjs.ct   | 144 +++++++++++++++++++++++++++++++++
 src/js_backend/js_backend_quickjs.h.ct |  31 +++++++
 src/js_host/js_host.ct                 |   7 ++
 src/js_host/js_host.h.ct               |   6 ++
 src/js_host/js_host_map.m.ct           |  20 -----
 src/units.json                         |   6 +-
 10 files changed, 293 insertions(+), 31 deletions(-)
```

现状（不是提案描述的那份）：

| 单元 | 现状 | 出处 |
| --- | --- | --- |
| `js_backend` | 新接口 + 两个同目录实现（`js_backend.ct` 空实现 / `js_backend_quickjs.ct` QuickJS），接口 4 个方法 `init/eval/shutdown/turns` | [js_backend.h.ct:10-20](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend.h.ct#L10-L20)、[units.json:10-15](file:///Users/samxx/trae-ws/nexcore/src/units.json#L10-L15) |
| `js_host` | **已标 DEPRECATED**，只剩 `.ct` + `.h.ct` 两个文件，`js_host_map.m.ct` 已删，不在 `units.json` 里 | [js_host.ct:7](file:///Users/samxx/trae-ws/nexcore/src/js_host/js_host.ct#L7) |
| `turns()` | 声明了、实现了，但 `src/NexCore.ct` **一次都没调用**（`grep -n turns src/NexCore.ct` 无命中）；quickjs 档实现恒定返回 `1` | [js_backend_quickjs.ct:141-144](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L141-L144) |

**判定**：提案的「现状摘要」整体落后 10 个提交。落后本身不致命——致命的是它在落后版本上推断了「什么已落地」，见 §3。

### 前提 4：CT 工具链版本与 `hosts/` 先例——**v0.44.2 可用；无 `hosts/`，先例在 `experiments/host/` 与 `tests/*.c`**

| 项 | 实测 |
| --- | --- |
| 基线版本 | `~/.local/ct/0.44.2/ct --version` → `ct v0.44.2`；声明见 [README.md:19](file:///Users/samxx/trae-ws/nexcore/docs/README.md#L19) |
| 系统默认 | `~/.local/bin/ct --version` → `ct v0.44.1`（不是基线，别拿它跑） |
| 多单元/可执行 | `ct build` 有 `-o <exe>` 与 `--lib`（`ct build --help` 原文） |
| `hosts/` | 不存在（`ls -d hosts` → No such file or directory）；`.gitignore` 无相关条目 |
| 既有宿主形态先例 | `experiments/host/abi_host.c`、`abi_host.swift`（实验）；`tests/*.c` 5 个 C `main` 真夹具（产品门禁内） |

**判定**：提案「仓库内还没有宿主目录」成立；「工程模式：测试夹具用纯 C 写 main」成立（但与「CT 不能写可执行」无关，见 §5）。

---

## 2. 三项要求的实测

### 2.0 前置：构建出可测的库

```
$ tar cf - --exclude=./build . | (cd /tmp/nxhost && tar xf -)      # 仓库根的 build/ 被 macOS 封存
$ curl -fsSL https://bellard.org/quickjs/quickjs-2025-09-13.tar.xz -o /tmp/qjs.tar.xz
$ make -C experiments/third_party/quickjs libquickjs.a -j4
$ PATH=~/.local/ct/0.44.2:$PATH NEXCORE_JS=quickjs bash tools/check.sh | tail -3
⑩ libnexcore: PASS（仅 libc/pthread，7 个 nexcore_* 导出）
ALL CHECKS PASSED (NEXCORE_JS=quickjs)
```

产物只有 `build/bin/libnexcore.dylib`；**仓库里没有 `libnexcore.a`**（`ls build/bin` → 7 个可执行 + `exports.txt` + `libnexcore.dylib`）。

### 2.1 公开 ABI 能否让宿主泵送微任务——**不能，ABI 结构上挡住了**

三层证据：

1. 公开 ABI 只有 7 个函数，**没有任何取 `JSContext`/`JSRuntime` 的入口**：[NexCore.h.ct:113-125](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L113-L125)（`create/set_sink/send/drain/shutdown/free/stat`）。`grep -i "loop|pump|job|context|runtime" build/abi/NexCore.h` 只命中统计键名，无函数。
2. `js_backend` 接口也只有 4 个方法，没有泵送方法：[js_backend.h.ct:10-20](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend.h.ct#L10-L20)。`turns()` 是唯一像个「turn」的东西，但**无人调用**（§1 前提 3）。
3. 实现里根本不泵：全仓 `JS_ExecutePendingJob` 只有一处，在探针 [bridge.ct:232](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L232)；`src/` 命中数为 0。
4. `js_std_loop` **确实可以被链接**（不是被 ABI 挡住链接，而是被挡住「拿到参数」）：

```
$ nm -g experiments/third_party/quickjs/libquickjs.a | grep -E "T _?js_std_loop|T _?JS_ExecutePendingJob"
0000000000001550 T _JS_ExecutePendingJob
00000000000018e4 T _js_std_loop
```

`js_std_loop(JSContext *ctx)` 需要一个 `JSContext*`，而 ABI 只给 `NexCoreHandle`（不透明），宿主无从取得 ctx。

**运行期实测**（探针只用 ABI，`/tmp/probe_microtask.c`）：

```c
cfg.bootstrap_js = "var hit=0; Promise.resolve().then(function(){hit=1;});";
/* 先空 drain 3 轮，再 send "hit" */
```
```
create rc=0
drain#0 rc=0 count=0
drain#1 rc=0 count=0
drain#2 rc=0 count=0
send rc=0
drain rc=0 count=1 sink.count=1 payload=0
```

`payload=0`：`.then` 从未执行，且**多 drain 几轮也不会执行**。

**结论**：宿主泵不了微任务。要让 `Promise` 可用，必须改库（新增泵送入口或让 `eval` 自带泵），这不是宿主侧工作。

### 2.2 Promise / pending / JS 线程任务队列现在在哪——**只在 `experiments/`，`src/` 零行**

| 能力 | `src/` | `experiments/` |
| --- | --- | --- |
| Promise 能力对象 | 0 处 | [bridge.ct:108](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L108)、[bridge.ct:147](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L147) `JS_NewPromiseCapability` |
| 微任务泵 | 0 处 | [bridge.ct:232](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L232) `while (JS_ExecutePendingJob(...))` |
| 工作线程 + job 槽 | 0 处 | [bridge.ct:131](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L131)、[bridge.ct:170](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L170) `pthread_create`；`Job jobs[16]` |
| 原生 Provider 注册 | **0 处** | [bridge.ct:240-248](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L240-L248) `register_native()` 挂 `log/nativeAdd/nativeFail/nativeAsync/...` |

`grep -rn -iE "promise|pending|microtask|std_loop|ExecutePendingJob" src/` 只命中统计键枚举名（`NX_STAT_PENDING_UI2JS` 等）与注释，无实现。

**结论**：提案「尚未落地（仅在设计/experiments）」这条**成立且可以再加一条**——「原生 Provider 注册机制」也不在 `src`，提案把它写进「已完成」是错的（§8 第 3 条）。

### 2.3 `check-boundaries.sh` 是否覆盖新建 `hosts/`——**不覆盖**

- 默认目标写死为 `src` 与 `experiments/src`：[check-boundaries.sh:15-18](file:///Users/samxx/trae-ws/nexcore/tools/check-boundaries.sh#L15-L18)。
- 门禁调用处还显式传参，等于二次排除：[check.sh:401](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L401) `bash tools/check-boundaries.sh "$SRC" experiments/src`。

实测（在 `/tmp/nxhost` 副本里造一个违规 `hosts/shell/demo.ct`，内容 `#if 1`）：

```
$ bash tools/check-boundaries.sh
boundaries: PASS（19 个文件，无 #if / 无 errno / import 顺序正确）      # rc=0，没扫到 hosts/
$ bash tools/check-boundaries.sh hosts
hosts/shell/demo.ct:1: 禁止条件编译：#if 1
boundaries: FAIL（1 处违规，扫描 1 个文件）                              # rc=1
```

**结论**：新增 `hosts/` 会成为一个**门禁盲区**。而 [roadmap.md:56](file:///Users/samxx/trae-ws/nexcore/docs/roadmap.md#L56) 写的是「**所有 `.ct`** 通过 `check-boundaries.sh`」——一旦 `hosts/` 里有 `.ct`，这句就变成假绿。

---

## 3. 提案没要求、但推翻阶段 1 的三项实测

### 3.1 脚本形态的输入能不能跑——**不能，只有单个表达式能跑**

探针 `/tmp/probe_script.c`（只用 ABI），每个用例一次 `send` + 一次 `drain`：

```
single-expr            send rc=0 drain rc=0 count=1 payload=2
multi-statement        send rc=0 drain rc=0 count=0 payload=<none>
function-decl          send rc=0 drain rc=0 count=0 payload=<none>
shebang-line           send rc=0 drain rc=0 count=0 payload=<none>
console.log            send rc=0 drain rc=0 count=0 payload=<none>
```

原因在实现，不在配置：

- `eval` 把源码**包一层括号**当表达式求值：[js_backend_quickjs.ct:87-99](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L87-L99)（`wrapped[0]='('` … `JS_EVAL_TYPE_GLOBAL`）。多语句/函数声明/`#!` 行在括号里必然语法错。
- 语法错之后**异常被吞掉**：`JS_GetException` 取到就 free，只返回 `-1`，文本不落地（[js_backend_quickjs.ct:101-105](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L101-L105)）。
- `NexCore.ct` 收到 `-1` 只写 `last_error = NX_E_INTERNAL`，**不投递任何东西给 sink**：[NexCore.ct:182-188](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L182-L188)。

也就是说：提案阶段 1 的「读取脚本文件，调用 `nexcore_load()` 加载脚本」「脚本异常打印堆栈，返回合理进程 exit code」——`nexcore_load` 这个 ABI 不存在，堆栈文本在库内被销毁，宿主要么拿到「无输出」，要么去轮询 `stat(LAST_ERROR)` 拿一个 `9`。**没有「打印堆栈」这一条路径。**

### 3.2 `Shell.print` 能不能由宿主注入——**不能，ABI 没有注册入口**

提案把「原生 Provider 注册机制：宿主可以注入 JS 可调用的原生能力」列为**已完成**。仓库事实：

- 7 个 ABI 函数里没有任何注册/绑定函数（[NexCore.h.ct:113-125](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L113-L125)）。
- `register_native()` 只在探针里（[bridge.ct:240-248](file:///Users/samxx/trae-ws/nexcore/experiments/src/bridge.ct#L240-L248)）。
- 连 `console` 都没有：`console.log('hi')` 的实测结果是 `count=0`（§3.1 第 5 行）——因为 `JS_NewContext` 之后从不装 std helpers。

**结论**：`Shell.print` 只能靠「改库（加注册 ABI）」实现，靠 bootstrap JS 也实现不了（JS 无法凭空调 C）。所以「阶段 1 对 `src` 零修改」与「阶段 1 提供 `Shell.print`」二者只能留一个。

### 3.3 方案 A 的前提「链接预编译完成的 `libnexcore.a`」——**这个产物不存在**

`tools/check.sh` 只产出 dylib（[check.sh:454-456](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L454-L456) `-dynamiclib -o "$BIN/libnexcore.dylib"`），`build/bin` 里没有 `.a`（§2.0）。另外注意 quickjs 档下 dylib **静态包含** 了引擎：`⑩` 用 `otool -L` 判「依赖里没有 quickjs」，静态链接天然过这条检查（[check.sh:458-461](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L458-L461)）——所以「libnexcore 不含 QuickJS」在产物面只对**动态依赖**成立，代码是在里面的。

---

## 4. 两席各打一轮

### 4.1 主张席（支持提案，含它可以自辩的最强版本）

1. **定位合法**：库的定位是「桥接层」，一个只读脚本、只调 ABI 的 CLI 不改变这个定位；`Shell.*` 全在宿主编译单元里，移动端不编、不链。
2. **对现状的直觉是对的**：它写「Promise/pending/任务队列不在 `src`」，这条被实测证实（§2.2）；一个外来评审能看准这一点，说明它的现状阅读不是全错。
3. **shebang 是真需求**：今天没有任何方式在 App 之外跑一段 JS 看桥接行为；`tests/*.c` 只能测 C 侧断言，不能跑脚本。
4. **A 的工程直觉可辩护**：宿主层是 argv/文件/信号/fork-exec 的胶水，C 最直接；仓库已有 `tests/*.c` 的范式，零新工具链风险。
5. **它主动提了边界与安全**（Shell 不下沉、沙箱默认关、警告不可信脚本），这三条与 D-11 的合规口径不冲突。

### 4.2 反证席（反对按现文开工）

1. **阶段 1 的「零修改」是自我否决**：`Shell.print` 要注册入口（§3.2），脚本入口要程序模式 eval（§3.1），异常堆栈要错误面（§3.1）——三件都在库内，且都不是「配置一下」能得到的。写成「零修改」会让评审误判工作量为零。
2. **「不依赖任何未落地库特性」是错的判据**：它依赖三样根本不存在的东西（注册入口、脚本入口、错误面），而这三样比 `experiments` 里的 pending 更靠前——后者至少有探针，前者连探针都没有（`bridge.ct` 也是表达式求值 + 自己 `register_native`）。
3. **`js_std_loop` 是错的泵**：NexCore 的语义是「同步 eval 一条消息」，泵微任务的正确位置在 `js_backend` 内部（`eval` 之后或独立的 `pump` 方法），而不是宿主从外面调 `js_std_loop`——宿主要调它就得拿到 ctx，而拿 ctx 意味着 ABI 被撕开。
4. **门禁会破**：`hosts/` 不在边界脚本扫描范围（§2.3），而 roadmap 要求「所有 `.ct`」；要么扩脚本（需要动编码侧独占的 `check.sh`），要么 `hosts/` 只许写 C（那就该明说）。
5. **阶段 3 把宿主塞进 `check.sh`** 与 `check.sh` 的归属约定冲突（[check.sh:5-12](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L5-L12)：编码侧唯一维护，新增检查项走需求），且会让「库门禁」依赖「宿主可构建」。应另立 `tools/check-host.sh`。
6. **维护成本论据已过期**：提案说 CT 写可执行「维护成本更高」，但 §4.6 实测 CT 能一次编 8 个单元出可执行；这条不能当理由（虽然结论仍可保留，见 §5）。

### 4.3 对打焦点

争点不是「A 还是 B」，而是「阶段 1 是否可独立开工」。主张席所有论证都建立在「阶段 1 用已落地能力」上；反证席用三条实测把这个前提拆了。**前提一倒，A/B 之争就从「选语言」降级为「选胶水语言」——那不是阻塞项。**

---

## 5. 裁定：A / B

**采纳方案 A（C 写宿主 main），但理由换三条**（提案给的理由「CT 可执行维护成本高」不采纳，已被 §4.6 证伪）：

1. **C 强迫宿主只走公开 ABI，这本身就是价值**。C 宿主 `#include` 的是 `build/abi/NexCore.h`，拿到的只有 7 个函数；它天然无法 `import NexCore;` 绕过 ABI 直接调内部单元。换成 CT 宿主，最省事的写法是 `import NexCore;` 直接调 `nexcore_*` 之外的内部符号，宿主就从「ABI 消费者」退化成「库的私有客户」，SDK 一改就静默失配。让 CLI 当 ABI 的第一公民，比省几行胶水重要。
2. **边界规则与宿主代码天然冲突**。`check-boundaries.sh` 禁 `#if/#ifdef`、禁 `errno`（[check-boundaries.sh:5-8](file:///Users/samxx/trae-ws/nexcore/tools/check-boundaries.sh#L5-L8)）。而 shell 宿主的核心恰恰是 POSIX 差异分支与 `errno` 判定（`execvp` 失败、`pipe/fork` 失败）。写 CT 就得把这些全塞进 `*_map.shim.h/.c`（`js_host_map` 的模式），等于把 C 藏起来，收益为零、审查面积更大。
3. **与既有范式一致**：`tests/*.c` 已经是「C main + CT 库」的产品级形态，门禁里跑得通；`hosts/shell/` 沿用同一形态，不新增工具链门禁。

**B 的地位**：不否决，降为备选，触发条件是「宿主需要与库共享 CT 类型或协议结构（例如复用 `protocol::Envelope`）」。此时成本是 `ct build` 多单元 + `--cflags` 转发（`-I` 不会自动进 `cc`，见 §4.6）。已实测可行，所以不是「做不到」，只是「现在不值得」。

**但 A 不是提案里的 A**：提案 A 假定链接 `libnexcore.a`（不存在，§3.3）。落地时二选一，都记一笔工作量：

- 直接链 `build/bin/libnexcore.dylib` + `-rpath`（零库改动，但发布形态与「静态二进制」诉求不同）；
- 或给 `check.sh` 增一个 `ar` 目标产出 `libnexcore.a`（注意 `.gitignore` 已忽略 `*.a`，产物路径要落在 `build/` 下）。

---

## 6. 三阶段划分：要改

### 6.1 「阶段 1 不依赖任何未落地库特性」今天成立吗——**不成立**

它依赖的三样，`src/` 里都没有，`experiments/` 里也没有同形态探针：

| 阶段 1 需要的 | 今天的状态 | 出处 |
| --- | --- | --- |
| 脚本（多语句/`#!`）入口 | 只有「单表达式」表达式求值 | §3.1 |
| 异常 → 堆栈文本 | 文本在库内被 free | [js_backend_quickjs.ct:101-105](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L101-L105) |
| `Shell.print` 的注册入口 | ABI 无注册函数 | §3.2 |
| 微任务泵 | 无 ABI 入口；`turns()` 是空转符号 | §2.1 |

### 6.2 建议的划分（阶段号沿用提案的 1/2/3，前面插一个阶段 0）

| 阶段 | 内容 | 出口判据（可核对） |
| --- | --- | --- |
| **0 库层契约（新增）** | 在 `js_backend` 上加两个方法：程序模式求值（不包括号）、错误面（`code + message` 出参）；在 ABI 上加原生函数注册（或等价的最小 Provider 回调表）；在 `js_backend` 内加微任务泵 | `NEXCORE_JS=quickjs` 档新增检查：`var a=1; a+1` 返回 `2`；语法错返回结构化错误且文案非空；`Promise.resolve().then` 的副作用可见；现有 ⓪–⑩ 全绿不变 |
| **1 宿主 MVP（C）** | `hosts/shell/main.c`：argv、读文件、`create/send/drain`、把阶段 0 的错误面打到 stderr、exit code；`Shell.print` 走阶段 0 的注册入口；`execSync` 用 `popen`（宿主内） | 若阶段 0 的注册入口就位，本阶段对 `src/` 零修改；`check-host.sh` 出 1 正 1 负两个用例 |
| **2 实用** | `-e`、`process.argv/env/__filename`、stdin、SIGINT 只置标记；`Shell.spawn` 的 Promise 化 | 依赖阶段 0 的泵 + D-05 的预算（`docs/decisions.md:85-110`），不依赖 `experiments` 迁移 |
| **3 生产/对齐契约** | sink 回调、stat、内存/CPU 上限、沙箱开关、字节码 | 门禁：**独立** `tools/check-host.sh`，由 `check.sh` 之外或末尾单点调用 |

**必须点明的一条顺序约束**：`Shell.spawn` 的 Promise 语义**排在阶段 0 的泵之后**。今天的实测（§2.1 `payload=0`）意味着：在没有泵之前，任何返回 Promise 的宿主 API 都会「永远 pending」，比同步阻塞更糟——脚本作者会以为是异步慢，而不是引擎没跑。

### 6.3 一个可选的降风险动作（不占阶段）

如果只想先验证 shebang 管道（内核怎么传 argv、`/usr/bin/env` 怎么解析），可以写一个**不连 NexCore** 的一次性 spike（裸 QuickJS + `js_std_add_helpers` + `js_std_loop`，`experiments/` 下现成）。它**不能**记作阶段 1，也**不能**进绿线——因为它测的不是库。这条是为了防止「拿 spike 冒充阶段 1」的假绿。

---

## 7. 四个开放问题，各一条明确意见

### 7.1 是否接受 `hosts/` 目录——**接受，但必须同一次提交里扩 `check-boundaries` 的扫描面**

- 目录名用 `hosts/shell/`，与 `tests/`、`experiments/` 平级；不要和 `experiments/host/` 混用（后者是探针，不是产品）。
- **必须同步**把 `tools/check-boundaries.sh:15-18` 的默认目标与 `tools/check.sh:401` 的显式实参加上 `hosts`，否则就是 §2.3 那个盲区。这是 `check.sh` 的改动，按 [check.sh:5-12](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L5-L12) 的归属约定要由编码侧落盘——所以这条应写成需求条目，不是宿主作者自己改。
- 若宿主按 §5 用 C 写，则 `hosts/` 里没有 `.ct`，`roadmap.md:56` 的「所有 `.ct`」不被违反，扩扫描面是「为将来兜底」，可以做但优先级低；**若宿主里出现任何 `.ct`，扩扫描面就是前置条件，不许省**。

### 7.2 异步子进程能力的库层优先级——**不因 shell 宿主提优先级**

- 库层的 pending/任务队列/超时/取消是 P2 范围的能力（[roadmap.md:73-80](file:///Users/samxx/trae-ws/nexcore/docs/roadmap.md#L73-L80)、D-04/D-05），驱动方是移动端宿主（iOS/Android 的真实 UI 阻塞问题），不是本地 CLI。
- CLI 可以先做同步 `execSync`，异步 `spawn` 挂在阶段 2 并**受阶段 0 泵的约束**（§6.2）。在泵没落地前，把异步能力排进计划就是排一个「永远 pending」的 API。
- 结论：顺序由库决定，不由宿主决定；宿主不得以「我需要」为由把 `experiments` 里的探针直接搬进 `src`（那是另一次带判据的库改动）。

### 7.3 二进制产物发布策略——**v1 不作为发布产出物，只作为本地开发工具**

- `nex` 不进发布清单、不做签名/公证书、不做包管理器分发；构建入口是本地脚本，产物落 `build/bin/nex`（`build/` 已被 `.gitignore`）。
- 理由三条：① D-11 的合规口径是「JS 只随包内置」，一个可下载/可分发的脚本宿主等于在桌面侧另开一条「运行任意脚本」的通道，容易被误读成与移动端口径矛盾；② 发布物需要版本号与 ABI 版本联动，会把 `NexCoreConfig`/ABI v1 的稳定性负担提前；③ P4 的交付是「接入契约 + 样例」，不是桌面工具链。
- 顺带一条硬事实：提案写 `#!/bin/nex`，那需要 root 写系统目录。实际应支持 `~/.local/bin/nex` 或 `#!/usr/bin/env nex`；`#!/bin/nex` 只作为文档里的「假设已安装」写法。

### 7.4 脚本后缀——**不约定后缀，只靠显式路径与 shebang**

- 接受 `nex path/to/anything`（后缀不参与分发、不做扩展名路由）；`.nexjs` 仅作文档示例。
- 理由：① 加扩展名路由会造出第二重身份（`nex a.txt` 到底跑不跑？），而 Linux shebang 只给解释器一个参数（提案 §4.2 自己也写了），参数面越窄越稳；② 与 node/python 的实际做法一致，误用成本低。
- 若将来确实要区分（例如 `.nexjs` 允许 `Shell.*`、其他后缀只跑纯 JS），那是**沙箱策略**问题，应做成显式开关，不要藏在后缀里。

---

## 8. 提案必须改的表述（逐条，按条目位置；本文件不改提案原文）

| # | 提案位置 | 现文 | 必须改成 | 依据 |
| --- | --- | --- | --- | --- |
| 1 | 一、2「已完成」第 2 条 | 「同步调用链路、消息信封协议、队列、配置结构体版本协商（struct_size）」 | 删掉「消息信封协议」或改写为「`protocol` 单元已编译但**未被 `NexCore.ct` 调用**；`send` 目前把消息当 JS 表达式求值，不做 method 分发」 | `grep -n protocol src/NexCore.ct` 只有一处 `protocol_version` 比较（[NexCore.ct:73](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L73)）；`pump_inbox` 直接 `eval(msg)`（[:182](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L182)） |
| 2 | 一、2「已完成」第 3 条 | 「原生 Provider 注册机制：宿主可以注入 JS 可调用的原生能力」 | 移到「尚未落地」，并注明「仅 `experiments/src/bridge.ct:240-248` 有探针，`src` 与 ABI 均无注册入口」 | §3.2 |
| 3 | 一、1 | 「对外 ABI 为纯 C，宿主无关」 | 保留，但补一句「当前 7 个函数，无脚本入口、无错误面、无 Provider 注册」 | [NexCore.h.ct:113-125](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L113-L125) |
| 4 | 一、4 | 「P1 阶段关键未决决策（阻塞项）/ QuickJS 依赖裁定 … 需要正式裁定」 | 改为「已由 D-21 裁定并落地（`834736a`、`4b943fd`），宿主侧只需面对 `js_backend` 接口」 | §1 前提 1 |
| 5 | 四、4.1 第 3 条 | 「需要先完成 P1 未决决策文档 `docs/reviews/nexcore-p1-quickjs-battle.md` 的裁定」 | 改为「裁定文档是 `docs/reviews/nexcore-p1-quickjs-ruling.md`；battle 文档是两席材料，第 2 行已写明不含裁定」 | §1 前提 2 |
| 6 | 二、2.1 第 3 条 | 「复用现有 `libnexcore` 全部能力：信封协议、RPC 调用、原生 Provider 注册、内存配额配置」 | 只保留「配置结构体（`struct_size` 协商）与队列」；其余三项标注未落地 | §3.2、上表第 1 条 |
| 7 | 二、2.2 方案 A | 「链接预编译完成的 `libnexcore.a`」 | 改为「链接 `build/bin/libnexcore.dylib`（当前唯一产物），或另立 `ar` 目标」 | §3.3 |
| 8 | 三、阶段 1 交付物 1 第 3 点 | 「执行顶层 JS 代码，泵送微任务 `js_std_loop`」 | 改为「需库层新增程序模式求值入口与微任务泵；宿主无法从 ABI 取到 `JSContext`，`js_std_loop` 不可调」 | §2.1、§3.1 |
| 9 | 三、阶段 1 交付物 2 | 「JS 注入 API（仅宿主内）`Shell.print` / `Shell.execSync`」 | 加前置：「依赖库层新增原生函数注册入口；当前 ABI 无此入口」 | §3.2 |
| 10 | 三、阶段 1 交付物 3 | 「脚本异常打印堆栈」 | 改为「异常文案需库层透出；当前实现 `JS_GetException` 后直接 free，宿主拿不到文本」 | [js_backend_quickjs.ct:101-105](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L101-L105) |
| 11 | 三、阶段 1 风险提示 | 「阶段 1 全部使用已落地的库能力，不依赖 experiments 内未合并的异步代码，可以快速验证」 | 整句删除或改为「阶段 1 依赖库层三项新增契约（脚本入口/错误面/注册入口），工作量主要在库不在宿主」 | §6.1 |
| 12 | 五、【重要】1 | 「阶段 1 MVP，对 `src` 库代码零修改」 | 改为「阶段 0 之后，阶段 1 才对 `src` 零修改」；或在阶段 1 明确放弃 `Shell.*` 与脚本执行 | §3.1、§3.2 |
| 13 | 五、4 | 「阶段 3：补充测试用例并入 `tools/check.sh` 门禁」 | 改为「另立 `tools/check-host.sh`；`check.sh` 由编码侧唯一维护，宿主用例不直接写进去」 | [check.sh:5-12](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L5-L12) |
| 14 | 四、4.4 第 1 条 | 「编译器版本 v0.44.1 约束」 | 改为「基线 `ct v0.44.2`（`328e5e63`）；系统 PATH 上的 v0.44.1 不是基线」 | [README.md:19](file:///Users/samxx/trae-ws/nexcore/docs/README.md#L19)、§1 前提 4 |
| 15 | 四、4.4 第 1 条（同上） | 「需要处理 CT 单元、units.json、外部 C 头导入、编译器版本约束，维护成本更高」 | 删掉这条作为选 A 的理由；实测 8 单元 `ct build` 出可执行通过。若仍选 A，改用 §5 的三条理由 | §4.6 |
| 16 | 四、4.3 第 1 条 | 「沙箱模式必须显式开启；默认模式下 `Shell.execSync`/`Shell.spawn` 完全开放」 | 保留，但补两句：「默认模式等同任意本地代码执行，故 `nex` 不作为发布物、不装系统目录（见 §7.3）」；「`#!/bin/nex` 需 root，实际用 `#!/usr/bin/env nex`」 | §7.3 |
| 17 | 六、2 | 「实现阶段 1 MVP（C 宿主）… 不触碰库层」 | 改为「先补库层契约（阶段 0），再写宿主；宿主不触碰库层的前提是阶段 0 已落地」 | §6.2 |

---

## 9. 本轮做了什么、没做什么

**做了**：读 D-21 / P1 裁定 / P2 收口 / P3 规划 / roadmap / decisions / status；比对 `808fc53..HEAD` 的 `src/` 差异；在 `/tmp/nxhost` 副本上构建 QuickJS `2025-09-13` 与 quickjs 档全量门禁（rc=0）；跑三个探针（微任务、脚本形态、CT 宿主链接 ABI）；在 `/tmp/rillct` 副本上复现 RILL 的多单元构建。

**没做**（按纪律）：未修改 `src/`、`tools/`、`docs/roadmap.md`、`docs/acceptance.md`、`.ai-engineering`；未新建 `hosts/`；未提交、未 push、未打 tag；未评价 P3/P4 范围以外的事。

**留待决策（不属本轮权限）**：
1. ABI 是否升 v2（新增脚本入口 / 错误面 / Provider 注册）——牵动 D-10、`NexCoreConfig` 与三端绑定，需单独裁定。
2. `libnexcore.a` 是否作为正式产物（`.gitignore` 已忽略 `*.a`，`check.sh` 需新增目标）。
3. 阶段 0 的判据要落进 `docs/acceptance.md` 还是先只在 phase 卡里——按 G1/G2 的现状，倾向后者先行。
