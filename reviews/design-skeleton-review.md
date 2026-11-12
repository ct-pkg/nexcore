# 编码骨架评审（第八轮）

- 评审对象：`src/`、`tools/`、`build/ct/` 产物，以及 `docs/` 的最新版
- CT 基线：v0.19.1（`ct v0.19.1`，`/Users/samxx/.local/bin/ct`）
- 评审方式：只读；全部实验在 `/tmp` 副本上完成，项目目录未写入
- 结论口径：区分「方案缺陷」与「表述问题」；查不到实证标「未验证」

## 0. 结论

| 级别 | 数量 | 说明 |
|---|---|---|
| P0 | **0** | 前向声明不带布局，Swift 实测通过；骨架能编译、能链接、smoke 通过 |
| P1 | 5 | 见 §7 |
| P2 | 10 | 见 §7 |

**一句话**：这个骨架能证明「ABI 形状 + 参数校验 + 跨单元映射单元」这条路是通的，但它**还不能跑任何一个业务场景**——没有 QuickJS、没有 JS 线程、没有出站投递（`queue` 连 `pop` 都没有，`drain` 从不调用 sink）、没有协议解析调用链、没有关闭序列。距离不是「补几个 TODO」，而是整个 Bridge 运行时还没开始。

---

## 1. ABI 头里的内部 struct 前置声明

### 1.1 根因判断：不是 import 闭包，是「本次构建的单元集合」

`docs/design.md:457` 写的是「`ct build --emit-c` 会把 import 闭包里**所有** struct 以前置声明写进头文件」。这个归因不成立，实测：

| 构建输入 | 生成的 `NexCore.h` 里的 `struct X;` |
|---|---|
| 只传 `src/NexCore.h.ct` | **只有 `struct NexCoreConfig;` 一条** |
| 只传 `src/NexCore.ct` | `struct NexCoreConfig;` + `struct NexCoreHandleImpl;` |
| 常规四单元一起构建 | `NexCoreConfig` / `NexCoreHandleImpl` / `queue_Ring` / `protocol_NxRequestEnvelope` / `protocol_NxResponseEnvelope`，**每个生成头都带全 5 条** |

关键反例：`src/NexCore.h.ct` 不 import `queue`、不 import `protocol`，但四单元一起构建时它的头里照样出现 `queue_Ring` 和 `protocol_*`。所以触发条件是**同一次 `ct build` 的输入集合里出现过这些 struct**，不是 import 传递闭包。`build/ct/queue.h` 里反而出现 `NexCoreHandleImpl`，同样证明是全局集合。

这个归因错误不是文字问题：design 由此推出的规避手段（「把内部实现拆出 ABI 单元的 import 闭包」）是无效的——`NexCore.h.ct` 本来就在闭包外面。

### 1.2 消费侧实测

**Swift（已实测）**

```
swiftc -typecheck -import-objc-header build/ct/NexCore.h
EXIT=0，无 warning、无 error
```

`NexCoreConfig` 直接可用（`struct_size` 等字段可访问），不需要 typedef。也验证了把 `struct NexCoreConfig;` 单独留在头里、只有前置声明 + 原型的 `.c`，在 `cc -std=c99 -Wall -Wextra -Werror -fsyntax-only` 下 `EXIT=0`。

没有出现「消费者以为要定义这些 struct」的情况：Clang importer 对不完整类型只生成不透明引用，不生成可构造类型；Swift 侧也没有为 `queue_Ring` 生成可见符号。

**Kotlin/JVM（未完成）**

`kotlinc`、`javac` 存在，`jextract` 不存在（未安装）。没有写 JNI 探针，因此「JNI 头生成器如何对待这些前置声明」**未验证**。就 C 侧而言，`ACC-A-06` 只要求 `System.loadLibrary` + 调 `nexcore_stat`，不要求解析头，风险面很小。

### 1.3 命名冲突风险

- C 的 struct tag 是**每个翻译单元独立**的命名空间，而且 tag 不产生链接符号；宿主另一个 `.c` 里定义 `struct queue_Ring` 不会与 Bridge 库冲突。
- 真正会炸的只有两种：① 宿主把 Bridge 的 `.c` 和自有 `struct queue_Ring` 定义**合并进同一个 TU**（amalgamation/unity build），会重定义；② 宿主在包含 `NexCore.h` 的同一个 TU 里定义同名且不同布局的 tag，然后又把该指针传给 Bridge——但 ABI 里没有任何函数接收 `queue_Ring *`，所以传不进去。
- 因此实际伤害接近零，但 `queue_`、`protocol_` 这种**单词级短前缀**确实容易被业务代码撞上（`protocol_` 尤其通用）。design `L470` 已经写了「平台代码不得声明 `struct queue_*`、`struct protocol_*`、`struct NexCoreHandleImpl`」，这条规则本身是够用的。
- 更彻底的做法是内部 struct 统一加 `nexcore_impl_` 前缀，或直接采用 §1.5 的干净头。按现状判 **P2（表述/卫生）**，不是 P1。

### 1.4 `NexCoreHandleImpl` 该不该暴露

- `docs/design.md:431` 的承诺是「句柄不透明；平台侧不知道内部布局」。前置声明只泄漏**名字**，不泄漏布局，`sizeof`/取值/按值传参都不成立，所以与「布局不透明」不冲突。
- 但它与「完全 opaque」的直觉相悖：消费者能看到「句柄背后有个 `NexCoreHandleImpl`」，这会诱导平台侧写出 `struct NexCoreHandleImpl *p = (struct NexCoreHandleImpl *)h;` 这种依赖——虽然不能解引用（不完整类型），但会把名字写进平台代码，之后重命名就是破坏性变更。
- 文档没有说这是「有意暴露」。`L472` 只是「接受 + golden」，属于事后追认，不是设计意图。
- 结论：**不是有意设计，是构建方式的副产物**；能去掉（§1.5），建议去掉。判 **P2**，去掉后本条消失。

### 1.5 能不能让导出头干净：能，而且已经跑通

不是「挪到不被 import 的位置」——`NexCore.h.ct` 不 import 任何内部单元，仍然被污染。有效做法是**按用途分两趟构建**：

```bash
# Pass 1：只导出 ABI 头（输入只有一个 .h.ct）
ct build --emit-c --ns-root src src/NexCore.h.ct -o build/abi

# Pass 2：生成实现 .c（输入全部 .ct，必须带 -I src）
ct build --emit-c --ns-root src -I src \
  src/NexCore.ct src/queue/queue.ct src/js_host/js_host.ct src/protocol/protocol.ct \
  -o build/impl
```

实测结果：

| 检查 | 结果 |
|---|---|
| `build/abi/NexCore.h` 的前置声明 | **只有 `struct NexCoreConfig;`**；`queue_Ring`/`protocol_*`/`NexCoreHandleImpl` 全部消失 |
| 与现有一趟构建的头 diff | 仅差这 4 条前置声明 |
| `cc -std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -I build/abi tests/smoke.c build/impl/*.c` | `EXIT=0`，零 warning |
| 运行 | `SMOKE OK`（`config: sizeof=72 v1_size=72`，create/send/stat/drain/shutdown/free 全通） |

两个注意点，都是可解的：

1. Pass 2 **必须带 `-I src`**，否则 `cannot resolve import 'queue/queue_map'`。`tools/check.sh` 已经带了。
2. `build/impl/` 下也有一份脏 `NexCore.h`；消费侧必须把 `build/abi` 放在 `-I` 首位。更稳的做法是 Pass 2 的 `-o` 用内部目录，发布时只装 `build/abi/NexCore.h`。

**因此 `docs/design.md:473` 的「内部类型名无法从 ABI 头中彻底移走……否则该现状不可消除」是错的**，这句直接导致 `ACC-A-01` 用 `abi/expected-forward-decls.txt` 把污染固化成 golden——一旦发布，后面再想清理就要改 ABI 验收。判 **P1**（方案缺陷，含表述错误）。

> 编码角色报的「这是 CT 对 import 闭包内所有 struct 的固定行为」，方向对了一半：是固定行为，但作用域是**构建单元集合**，不是 import 闭包。修法也因此不同。

---

## 2. 其余六点

### 2.1 `#define` 不进导出头：成立；`pub enum` 可替代，但值是 `int`

实测：`queue.ct` 里的 `NXQ_ERR_ARG`/`NXQ_ERR_FULL`/`NXQ_ERR_MEM`、`NexCore.ct` 里的 `NEXCORE_HANDLE_MAGIC`、`NX_QUEUE_FULL` 只出现在生成的 `.c`，`build/ct/*.h` 里一个都没有。`pub enum` 的成员完整出现在头里（`enum NxCode`、`enum NxAbi`、`enum NxLimit`、`enum NxStatKey`）。

枚举值的类型：C99 里枚举常量类型是 `int`（实现可选用能容纳所有值的整型，但常量表达式按 `int` 处理）。这对当前用途没有伤害：

- `abi_version`/`protocol_version` 在结构体里是 `uint32_t` 字段，枚举只用于赋值和比较；
- `NEXCORE_CONFIG_V1_SIZE = offsetof(...) + sizeof(size_t)` 已经实测可编译（smoke 打印 `v1_size=72`），说明 CT 允许 `offsetof`/`sizeof` 进入 enum 常量表达式；所以 design `L521-523` 的 `CURRENT_SIZE = sizeof(struct NexCoreConfig)` 在 CT 下**可行**。

需要注意的只有一条：**不要用枚举常量去表达「宽度」**。`sizeof(NEXCORE_ABI_VERSION)` 恒为 `sizeof(int)`，不是 4 字节的 ABI 常量；平台侧若生成 `MemoryLayout<NEXCORE_ABI_VERSION>.size` 会得到 `Int` 的宽度。ABI 版本的正确落点是 `uint32_t` 字段 + `_Static_assert`，文档应补这一句。判 **P2（表述）**。

### 2.2 `NexCoreConfig` 无 typedef：结论对，理由错

`docs/design.md:450` 写「CT 不支持 `pub typedef struct X X`（判为重复 struct）」。实测三种构造（均 `--ns-root` 对齐、单元在 `t/` 目录下）：

| 写法 | 结果 |
|---|---|
| `pub typedef struct T T;` + `pub struct T { ... }` | `error: unexpected token`（指向别名 `T`，列 22），**不是重复** |
| 只有 `pub typedef struct T T;` | 同上 `unexpected token` |
| 先 `pub struct T { ... }` 再 `pub typedef struct T T;` | `error: duplicate struct 'T'` |
| `pub typedef struct T;`（无别名） | `EXIT=1`，**stdout/stderr 全空**，零诊断 |
| `pub typedef struct { int32_t a; } T;` | 可编译，头里是 `typedef struct { int32_t a; } t_T;`（匿名，无法前置声明） |
| `pub struct T;` | 可编译，头里是 `struct t_T;`（CT 的正确不透明声明写法） |

所以真实规则是：**CT 的 typedef 不接受 `struct Tag Alias` 形式；`struct Tag` 会被当成一次新的不透明 struct 声明，与已有定义撞车。** 「判为重复」只在顺序相反时成立。

对平台侧的影响：结论不变——C 侧必须写 `struct NexCoreConfig`，Swift 经 Clang importer 得到 `NexCoreConfig`（已实测），Kotlin/JNI 的 C 侧同样写 `struct`。但文档给出的「为什么」是错的，而且漏掉了 `pub struct T;` 这个正确的不透明声明写法（将来若要暴露不透明类型会用到）。判 **P2（表述）**。

### 2.3 `.h.ct`/`.ct` 是否自动配对 + 同名 `.c` 谁胜出

实测两种顺序（`NexCore.h.ct` 在前 / `NexCore.ct` 在前）生成的 `.h` 与 `.c` **`diff` 均为空**，`EXIT=0`。顺序不影响产物，编码说的「无文档保证」不成立——`docs/roadmap.md:33` 和 `docs/codegen.md:619` 都明确写了「`.ct` 必须显式列输入，`.h.ct` 不自动配对同目录 `.c`」。

但更关键的机制编码没抓到：**实现 `.ct` 里的 `import <self>` 才是配对的唯一开关**。见 §2.4。本条判 **P2（表述：编码的结论不成立，文档其实有写）**，不列 P1。

### 2.4 「实现 `.ct` 必须 `import <self>`」：是唯一途径，且失败是静默的

实测：把 `/tmp` 副本的 `queue/queue.ct` 第一行 `import queue;` 删掉，其余不动，用与项目相同的四单元命令构建：

```
ct build --emit-c ... -o out_noimport
EXIT=0                      ← CT 不报错
产物：js_host.c js_host.h NexCore.c NexCore.h protocol.c protocol.h queue.c
      ← queue.h 消失
```

产物是坏的：

- `nexcore_send` 的实现里调用变成裸名 `push(...)`，符号从 `queue_push` 退化为 `push`（**前缀丢失**）；
- `queue.c` 不再 `#include "queue.h"`；
- `NexCore.c:3` / `js_host.c` 的 `#include "queue.h"` 直接 `fatal error: 'queue.h' file not found`。

反向验证：命令行显式传 `queue.h.ct`（即使 `.ct` 里没有 self import）能恢复 `queue.h` 和前缀，但 `queue.c` 仍**不 include 自己的头**。所以「自导入」不是可选优化，而是同时决定三件事：生成头、名字 mangling、`.c` 自包含。

**结论**：`.h.ct` 不会被隐式包含；`import <self>` 是唯一配对途径；漏写时 CT exit 0、由 `cc` 兜底。检查 ⑤ 应拆成三条而不是现在这一条：

1. 命名空间实现单元**必须** `import <self>`（新增，当前完全没检查）；
2. 允许 `import <ns>::<ns>_map`；
3. 禁止 import 其他实现单元。

判 **P1**（与 §3 的检查⑤ 合并）。

### 2.5 命名空间必须匹配目录：成立

实测两条：

```
src/bridge/js_host/{js_host.h.ct,js_host.ct}  → EXIT=1
  ctc: namespace 'js_host' does not match the directory 'bridge/js_host'

src/js_host.h.ct（平铺到 src 根）              → EXIT=1
  ctc: namespace 'js_host' does not match the directory ''
```

所以 `bridge/js_host.h.ct` 平铺确实被拒，报错是 `namespace ... does not match the directory ...`。注意 `--ns-root` 是基准：`--ns-root src` 时，`src/bridge/js_host` 相对路径就是 `bridge/js_host`。本条**核实无误，无问题**。

### 2.6 检查 ⑤ 的口径：文档写窄了，实现宽了，且 map 整类跳过

`tools/check.sh` 第 5 段的实际规则是：

- 跳过 `_map.m.ct` 与根单元；
- 允许 `last == self_name`（自导入）或 `last.endswith("_map")`；
- 其他一律报「命名空间单元 import 了非映射单元」。

所以「只 import `.m.ct`」这句是错的，应拆成 §2.4 的三条。更严重的是：

```
在 src/queue/queue_map.m.ct 里加 import queue;  → check.sh EXIT=0（PASS）
在 src/js_host/js_host.ct 里加 import queue;    → check.sh EXIT=1（正确）
```

原因是脚本对 `_map.m.ct` 整类 `continue`，**「map 不得 import 实现单元」这条从未被检查**。而 design `L184` 明确说 map 误用会导致链接失败，这是真实风险。判 **P1**。

### 2.7 `units.json` 替代 YAML：格式没问题，双源才是问题

判断：当前用 JSON 合理，P3 接 schema 不会因为「JSON vs YAML」返工——`nexcore_gen.py` 读的是结构，换格式只是一行 `yaml.safe_load`。真正会返工的是结构：

- `units.json` 的 `methods` 与 `.h.ct` 的 `pub` 函数是**两份真相**；
- `render_unit` 只遍历 `unit.methods`，`.h.ct` 里新增、但没写进 `methods` 的 `pub` 函数会被**静默漏掉**：不生成 recipe，`--check` 也不会报（因为它只比对自己生成的产物）；
- `build_symbols` 反而会把这些函数收进符号表，于是 ④ 的 callee 断言对「没被生成的函数」形成盲区。

建议：保留 `units.json` 作为**拓扑**输入（namespace/trait/source），`methods` 改为「从 `.h.ct` 解析后断言与清单一致」，或干脆删掉 `methods` 由解析结果驱动。P3 接 schema 时再加 `schema` 字段即可，不需要重写。判 **P1（完整性检查缺失）**，格式本身 **P2**。

---

## 3. 五条机械检查的负例复跑

基线：`bash tools/check.sh` → `EXIT=0`，① PASS、② PASS 4 files、③ PASS 4 files、④ PASS 7 recipes、⑤ PASS 6 files、`SMOKE OK`。

| 检查 | 负例构造 | 结果 | 判定 |
|---|---|---|---|
| ① `ct build` 退出码 | 在 `.ct` 里插语法错误 | `EXIT=1`，`error: expected a type (tok 4 '}')` | 有效 |
| ② `-Werror=implicit-function-declaration` | `.c` 里调用未声明函数 | `EXIT=1`，`call to undeclared function` | 有效，但只 `-fsyntax-only`，不覆盖链接期缺失/重复定义 |
| ③ 生成 C 无 `::` | 注释里写 `queue::Ring`、字符串里写 `"::"` | `EXIT=0`（不报）；真实 `a::b` 报 1 处并 `EXIT=1` | 有效，剥注释/字面量的实现正确 |
| ④ recipe callee == 符号表 cname | 把 recipe 改成 `queue_pushx` | 用完整副本 `--spec srcC --src srcC` 时 `EXIT=1`（正确）；只改临时目录、仍用 `--src src` 时 `EXIT=0` | 有效，但作用域绑死 `--src`，且只比 callee 名，不比参数个数/类型/调用点 |
| ⑤ 命名空间单元只 import `.m.ct` | 实现单元 import 实现单元 / map 文件 import 实现单元 | 前者 `EXIT=1`；后者 **`EXIT=0`（漏检）** | 部分有效，见 §2.6 |

补充结论：③④ 的负例不是编码角色构造的假象，方向都对；但 ⑤ 存在真实漏检，④ 的覆盖面比文档描述窄。

---

## 4. 上一轮 4 条 P1 / 9 条 P2 的状态

### P1

| 上轮 | 内容 | 现状 |
|---|---|---|
| P1-1 | transport 最小接口未定义 | **仍未改**：design §1.3 只有一句契约，接口仍推到 roadmap P4 |
| P1-2 | `pump_jobs` closing 分支 | **已改**：`docs/async-threading.md:169-196` 有 `!closing` / `closing` 分支，`r<0` 按 `close_abort`→`abort_requested`→异常分类 |
| P1-3 | FATAL 释放 runtime 时机 | **已改**：`L258-274` 明确「job 返回、JS 栈展开、回到 turn 边界后才 `JS_Free*` 并置 `js_done`」 |
| P1-4 | ACC-C-11 的 200 语义重叠 | **已改**：`async-threading.md:199-210` 拆成 `pump_budget_ms` / `shutdown_budget_ms` / `shutdown_hard_increment_ms` / `shutdown_schedule_slack_ms` 四个具名常量；`acceptance.md:98` 也逐名写全 |

### P2

| 上轮 | 内容 | 现状 |
|---|---|---|
| 1 | `close_abort` 未写「只由 JS 线程 interrupt handler 读写 / 不采用外部 watchdog / 若采用必须 `_Atomic`」 | **未改**：全文无 `_Atomic`、无 `watchdog` |
| 2 | `NX_STAT_CORE_BLOCK` 无法产生 | **已收口**：`async-threading.md:237` 明确「不提供」 |
| 3 | 「Core 不得阻塞」未在绑定/生成物中体现 | **未改**：无 DEBUG 断言或进入时间打点 |
| 4 | abnormal 分支掩盖原因 | **部分改**：`design.md:583` 增加 `JS_THREAD_ABORTED` + `last_turn_seq`/`last_job_id`/`last_method` |
| 5 | JS 线程内结束自己的完整用法未成句 | **已改**：`async-threading.md:291-298` |
| 6 | 「map 误当实现单元」措辞混淆 import 与加入构建 | **已改**：`design.md:184` 已限定为「链接会因缺少 cname 定义而失败」 |
| 7 | 生成器需声明「必须在全量单元上运行」 | **未改**：全文无此约束 |
| 8 | 多 trait 同签名共享 cname 未标 alias | **未改** |
| 9 | `decisions.md:208` 仍写「只更新 `V1_SIZE`」 | **未改**，与 D-18 `L370-372`、design `L441` 冲突 |

---

## 5. 骨架与设计的 ABI 漂移（本轮新增，P1）

需求和编码是并行推进的，`docs/` 已经往前走了，`src/` 停在旧版。这不是「编码 bug」，但必须先对齐，否则后面每加一个功能都要改 ABI：

| 设计（`docs/`） | 骨架（`src/NexCore.h.ct` / 生成头） | 后果 |
|---|---|---|
| `NX_E_CLOSE_TIMEOUT = 11`（design `L520`） | 枚举只到 `NX_E_BUSY = 10` | ACC-C-11 无法用 ABI 常量实现/断言；平台侧拿不到这个错误码 |
| `struct NexCoreConfig` 尾部有 `NexCoreLog log; void *log_user;`（design `L499-501`） | 没有这两个字段，`sizeof` 72 | 平台按文档填 `struct_size` 会与库不一致；`log` 永远不可用 |
| `NEXCORE_CONFIG_MIN_SIZE` / `CURRENT_SIZE`（design `L521-523`） | 只有 `V1_SIZE` | 平台侧无法做 `CURRENT_SIZE == sizeof` 断言；design 要求的双向兼容没有可检查的常量 |
| `_Static_assert(CURRENT_SIZE == sizeof(struct NexCoreConfig))`（design `L443`） | `sizeof == V1_SIZE`（`NexCore.ct:183-184`） | 一旦补上 `log/log_user`，这条断言必然失败；现在它编码的是**旧的**规则 |
| 统计键 18 项（design `L573-590`，含 `CLOSE_HARD_ABORT`/`JS_THREAD_ABORTED`/`JS_RUNAWAY`/`PUMP_BUDGET`/`TIMEOUT`） | 12 项（0..12），缺 5 个，且顺序不同 | `nexcore_stat` 的 `int32_t key` 是 ABI；编号一旦被平台绑定就不能改。设计侧的清单**没有编号**，等于编号契约缺失 |

其中「统计键无编号」本身是设计缺口：`nexcore_stat` 的 key 是跨 ABI 的整数，必须有一张冻结的 `key → 数值` 表（可以放在 `pub enum` 里），否则平台绑定和 golden 都无从谈起。建议把 design `L573-590` 的表补上数值列，并让 `src/NexCore.h.ct` 的 `pub enum NxStatKey` 与之逐项一致。

判 **P1**（方案缺陷：ABI 面不一致 + 统计键无编号契约）。

---

## 6. 这个骨架离「能真跑一个业务场景」还差什么

先说骨架现在**真正能做什么**（基于 `src/` 与 smoke 输出）：

1. 校验 `NexCoreConfig` 的 `struct_size`/`abi_version`/`protocol_version`/`reserved`/`flags`；
2. 分配一个 `queue_Ring`，把 `bootstrap` 标记、`send` 的原始字符串、`shutdown` 标记压进去；
3. 维护 `state`/`last_error`，返回 `NX_E_SHUTDOWN`/`NX_E_BACKPRESSURE`/`NX_E_PAYLOAD_TOO_LARGE`；
4. 统计 inbox depth 与 last_error；
5. 按映射单元跨单元调用（`queue::Ring.*`、`js_host::Host.*`），证明 b′ 拓扑可编译、可链接。

然后逐项对照一个最小业务场景（UI 发一条 `{"id":1,"method":"app.ping"}` → JS 执行 → 回响应）：

| 业务场景需要 | 骨架现状 | 差距 |
|---|---|---|
| 启动 QuickJS、eval bootstrap | `js_host_init(q, bootstrap_len)` 只把长度当参数，内部 `queue_push(q, "bootstrap", 9)`；`bootstrap_js` 内容从未被读取 | **整个 QuickJS 集成**（runtime/context/eval/interrupt handler） |
| JS 线程 | `nexcore_create` 不创建线程；`shutdown` 无 join；`free` 无 `drain_active` 检查 | **整个线程生命周期 + 关闭序列**（stop/abort/close_abort、grace 500 + hard 200、DEAD 单点） |
| 入站消息解析 | `nexcore_send` 只复制字符串，不解析 JSON、不校验 id/method/方向 | **turn 的全部逻辑**（parse → 校验 → pending → dispatch） |
| 出站消息投递 | `queue` 只有 `open/push/depth/close`，**没有 `pop`**；`nexcore_drain` 忽略 `max_wait_ms`、`*out_count=0`、**从不调用 sink** | **出站队列 + 阻塞/唤醒 + sink 调用**；现在平台一条消息都收不到 |
| 协议层 | `protocol.ct` 里有 `protocol_request_well_formed`/`protocol_response_kind`，但 `NexCore.ct` **没有 import protocol**，从未调用 | 协议单元是死代码；JSON 编解码/错误映射全缺 |
| 超时/取消/晚到响应 | `default_timeout_ms` 未使用；无 pending map | **整个 §4/§6**（pending、deadline、`E_TIMEOUT`、`E_CANCELLED`、`DROPPED_LATE`） |
| 背压 | `max_pending` 未使用，队列容量直接用 `NEXCORE_DEFAULT_MAX_PENDING`；doc 要求 inbox 4096 条 / 8 MiB | 容量策略与背压计数都未实现 |
| 线程安全 | `queue_push`/`queue_depth`/`queue_close` 全无锁、无 condvar；doc 要求 MPSC | **整个队列实现**要重写；现在多线程 `send` 就是数据竞争 |
| schema 校验 | `cfg->schema_hash` 完全没读 | `NX_E_SCHEMA_MISMATCH` / ACC-A-11 不可达 |
| 时钟注入 | `cfg->now_ms` 没读 | ACC-A-09 不可达 |
| 可观测性 | 12 个键里只有 2 个有值，其余硬编码 0 | 平台会把「未实现」读成「真实 0」，比不返回更危险（见 P2-10） |
| 重入 | 从不返回 `NX_E_REENTRANT` | ACC-C-07 不可达 |
| 平台接入 | 无 Swift/Kotlin 绑定、无 transport | ACC-A-05/06/07、G 组全部不可达 |

**判断**：骨架完成的是「ABI 形状 + 参数校验 + 拓扑验证」，大约是一个 Bridge 的 10%；剩下 90% 是运行时，不是补丁。要跑通业务场景，最小闭环至少要有：QuickJS 线程 + inbox/outbox 队列（带锁与 condvar）+ 协议解析 + pending/timeout + sink 投递 + 两阶段关闭。这六件事缺一件都跑不起来。

**建议的下一步顺序**（按依赖，不按文档章节）：
1. 队列换成真正的 MPSC（mutex + condvar + 容量/字节双上限 + `pop` + `close` 唤醒）；
2. JS 线程 + QuickJS 生命周期（create/destroy/`JS_UpdateStackTop`/interrupt handler）；
3. turn：解析 → dispatch → pump → 超时；
4. outbox + `nexcore_drain` 阻塞与 sink；
5. 关闭 7 步（含 grace/hard、join、DEAD 单点）；
6. 统计键按设计要求补齐，再接验收。

---

## 7. 问题清单

### P0（0）

**无。** 导出头带内部前置声明不构成 P0：不泄漏布局，Swift 实测通过，C 侧 `-Werror` 通过，两趟构建可消除。

### P1（5）

1. **（方案缺陷）导出头污染的归因与「不可消除」结论都错，且被验收固化**
   - `docs/design.md:457` 说「import 闭包」，实测是「构建单元集合」；
   - `docs/design.md:473` 说「无法从 ABI 头彻底移走」，实测两趟构建可行（干净头 + `SMOKE OK`）；
   - `docs/acceptance.md:60` 的 `abi/expected-forward-decls.txt` 把污染写成 golden，先发布再清理就是破坏性变更。
   - 修法：`codegen.md:617-627` 拆成 Pass 1（只 `.h.ct` → 发布头）与 Pass 2（`.ct` → 实现），`check.sh` 同步；`ACC-A-01` 改为断言「ABI 头只有 `struct NexCoreConfig;`」。
2. **（方案缺陷）检查 ⑤ 的口径与实现不符，且漏检 map 导入实现单元**
   - 实际规则是「允许 self + `_map`」，文档写「只 import `.m.ct`」；
   - `_map.m.ct` 被整类跳过，实测「map 里加 `import queue;`」仍 PASS；
   - 缺「实现单元必须 `import <self>`」断言。实测删除自导入后 **CT exit 0**，但产物没有 `queue.h`、函数丢 `queue_` 前缀，下游 `cc` 才 fatal error。
3. **（方案缺陷）骨架 ABI 面与当前设计漂移**
   - 缺 `NX_E_CLOSE_TIMEOUT = 11`；缺 `log`/`log_user`；缺 `MIN_SIZE`/`CURRENT_SIZE`；
   - `NexCore.ct:183-184` 的 `_Static_assert(sizeof == V1_SIZE)` 编码的是旧规则，补上尾字段后必然失败；
   - 统计键 12 项 vs 设计 18 项，且**设计侧没有给 key 编号**（`nexcore_stat` 的 key 是 ABI，必须有冻结数值表）。
4. **（方案缺陷）`units.json` 的 `methods` 与 `.h.ct` 是双源，静默漏方法**
   - `render_unit` 只遍历 `unit.methods`；`.h.ct` 新增但未列入的 `pub` 函数不生成 recipe，`--check` 也不报；
   - `build_symbols` 却会收录它，导致 ④ 对未生成函数形成盲区。
   - 修法：由 `.h.ct` 解析驱动，`methods` 只做一致性断言。
5. **（方案缺陷，上轮遗留）transport 最小接口仍未定义**
   - `docs/design.md` §1.3 只有契约描述；`docs/roadmap.md:182` 推到 P4。平台接入无法按文档开工。

### P2（10）

1. **（表述）`docs/design.md:450` 的 typedef 理由错**：实测 `pub typedef struct X X;` 报 `unexpected token`，不是「重复 struct」；只有「先 struct 后 typedef」才报 `duplicate struct`。补上 `pub struct X;` 这个正确的不透明声明写法。
2. **（表述）枚举常量的宽度**：C 枚举常量是 `int`。`NEXCORE_CONFIG_*` 用 enum 可行（`offsetof`/`sizeof` 已实测可进 enum），但不要用 `sizeof(ENUM_CONST)` 表达 ABI 宽度；ABI 版本应由 `uint32_t` 字段 + `_Static_assert` 承载。
3. **（表述）编码的「`.h.ct`/`.ct` 无文档保证」不成立**：`docs/roadmap.md:33`、`docs/codegen.md:619` 都写了不自动配对；两种输入顺序实测产物 `diff` 为空。
4. **（表述）短前缀命名污染**：`queue_`/`protocol_` 是单词级前缀，宿主 unity build 下会重定义。design `L470` 的禁令可用，但更稳的是内部 struct 统一加 `nexcore_impl_` 前缀，或采用 §1.5 的干净头。
5. **（方案）`NexCoreHandleImpl` 暴露**：与「布局不透明」不冲突，与「完全 opaque」冲突；无文档说明是有意。两趟构建后自动消失。
6. **（方案）`nexcore_stat` 对未实现键返回 0**：`NexCore.ct:174-177` 把 1..11 全部硬编码 0，平台会读成「真实值为 0」。未实现的键应返回 `NX_E_INVALID_ARGUMENT` 或明确的 `NX_STAT_UNSUPPORTED`，或在文档里写死「0 表示未实现」。
7. **（方案）`nexcore_create` 忽略多个配置字段**：`now_ms`、`default_timeout_ms`、`max_pending`、`schema_hash` 均未使用；`max_pending` 被固定成队列容量 1024，与 doc 的 inbox 4096 条/8 MiB 不一致。
8. **（方案）检查 ② 只做 `-fsyntax-only`**：不覆盖「声明了但没定义」「重复定义」「符号白名单」。`ACC-A-02` 的 `nm` 白名单没有进脚本。
9. **（表述）检查 ④ 只比 callee 名**：不比参数个数/类型/调用点，且 verify 只读 `--src` 树；建议补「recipe 签名 == 符号表签名」。
10. **（表述）文档版本记录滞后**：`docs/README.md` 状态行与变更记录停在 v0.6，但正文已加入 `log/log_user`、`MIN/CURRENT`、前置声明处置、四个关闭常量等新内容；`docs/decisions.md:208` 仍写「追加可选字段只更新 `V1_SIZE`」，与 D-18 `L370-372`、design `L441` 冲突。

---

## 8. 取证附录

环境：`ct v0.19.1`、macOS arm64、Swift 6.3.3、`kotlinc`/`javac` 存在、`jextract` 不存在。

关键命令与结果：

```
# 基线
bash tools/check.sh                                          → EXIT=0（①~⑤ + SMOKE OK）

# 头隔离
ct build --emit-c --ns-root src src/NexCore.h.ct -o /tmp/t3/abi
  → abi/NexCore.h 仅 struct NexCoreConfig;（1 条前置声明）
ct build --emit-c --ns-root src -I src src/NexCore.ct src/queue/queue.ct \
  src/js_host/js_host.ct src/protocol/protocol.ct -o /tmp/t3/impl   → EXIT=0
cc -std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration \
  -Werror=incompatible-pointer-types -I /tmp/t3/abi tests/smoke.c /tmp/t3/impl/*.c \
  -o /tmp/t3/smoke && /tmp/t3/smoke                            → EXIT=0，SMOKE OK

# Swift
swiftc -typecheck -import-objc-header build/ct/NexCore.h        → EXIT=0，零诊断

# typedef
pub typedef struct T T;                    → error: unexpected token (5:22)
pub struct T {...}; pub typedef struct T T; → error: duplicate struct 'T'
pub typedef struct T;                      → EXIT=1，零输出
pub struct T;                              → EXIT=0，头里 struct t_T;

# 自导入
（删 queue.ct 的 import queue;）ct build ...  → EXIT=0，但无 queue.h，符号退化为 push/open
cc -fsyntax-only NexCore.c                   → fatal error: 'queue.h' file not found

# 命名空间/目录
src/bridge/js_host/*                         → ctc: namespace 'js_host' does not match the directory 'bridge/js_host'
src/js_host.h.ct                             → ctc: namespace 'js_host' does not match the directory ''

# 顺序
先 .h.ct 后 .ct / 先 .ct 后 .h.ct           → 两次生成的 .h/.c diff 均为空

# 检查⑤
queue_map.m.ct 里加 import queue;           → check.sh EXIT=0（漏检）
js_host.ct 里加 import queue;               → check.sh EXIT=1（正确）

# 检查①②③④
语法错误 → ct build EXIT=1；隐式声明 → cc EXIT=1；
注释/字符串 :: 不报、真实 :: 报；完整副本改 queue_pushx → EXIT=1
```

未验证项：

- Kotlin/JVM 通过头生成（jextract 缺失）——未验证；
- `pub typedef struct X X` 在「struct 定义之前」是否有其他形态可用（只测了三种）——已足够回答当前问题，但非穷举；
- 两趟构建对「全量发布物（含生成器 C、platform 绑定）」的端到端影响——只验到 smoke。
