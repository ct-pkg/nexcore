# 总体设计

## 1. 目标与非目标

### 1.1 目标

ct-NexCore 是**框架**，不是业务核心。它要交付的是让宿主项目的业务 core 能在原生 UI 上跑起来的那层能力：

1. 提供跨 macOS/iOS 与 Android 的桥接层：宿主业务 core 与宿主原生 UI 之间只依赖框架契约。
2. 交付 **QuickJS 运行时集成**：单 JS 线程、生命周期、限额、关闭与错误隔离。
3. 交付 **稳定 C ABI**：7 个函数、不透明句柄、冻结的 `NexCoreConfig`。
4. 交付 **JSON 协议**：信封、错误码、订阅/事件、超时/取消/背压。
5. 交付 **代码生成器 CLI**：把宿主定义的 Schema 变成 C 注册代码与 Swift/Kotlin/JS 绑定。
6. 交付 **结构化调试日志**：错误码、统计键、调用链定位。
7. 桥接层是**固定模式**：结构不随业务增长而变化，任何一次改动都能按条目审查；生成物可逐行审阅。
8. 框架边界让 AI 可以大量生成**宿主侧**业务 JS、测试和调用代码，但不能越过桥接层与线程/生命周期边界。

框架只保证“跑得起来、调得到、契约不出错”，**不保证业务结果正确**；业务正确性由宿主项目自己的测试负责。

### 1.2 非目标

- 不交付业务 core（CT/C 实现）、业务 JS、UI 代码或业务 Schema 内容；这些全部属于宿主项目。
- 不实现校验、状态机、计价、流程编排等业务逻辑；框架只提供承载它们的运行时与契约。
- 不做跨平台 UI 框架；UI 仍是宿主侧的 SwiftUI/Compose。
- 不做通用 RPC 框架；协议只服务于宿主 UI↔宿主 JS 与宿主 JS↔宿主 Core。
- 不在 v1 支持多 JS 运行时共享、远程调试协议、热重载、插件市场。
- 不让宿主 UI 直接调用宿主 Core（必须经过 JS；理由见 §3.5 与 D-13）。
- 不在首批支持 Linux/Windows 桌面；线程 shim 预留接口，但不验收。

### 1.3 框架交付物与宿主职责

| 归属 | 交付/负责内容 | 不在其中 |
|---|---|---|
| **框架（ct-NexCore）** | QuickJS 运行时集成；C ABI 桥接层；JSON 协议；代码生成器 CLI；结构化调试日志；固定模式与生成规则；transport 契约（drain 线程模型与最小接口） | 业务 core、业务 JS、UI、Schema 内容、各平台 transport 实现 |
| **宿主项目** | 业务 core（CT/C 或其他可被 C ABI 调用的实现）；业务 JS 编排；原生 UI；业务 Schema（方法/事件/类型的具体内容）；各平台 transport 实现；业务测试 | 桥接层内部、线程/关闭顺序、协议实现、生成器模板 |

框架对宿主提供的是“**能力与约束**”：Schema 形态、生成器规则、可检查项、协议与线程契约；不是“生成好的业务内容”。

**transport 最小契约**（框架交付“契约”，各平台实现由宿主负责；具体函数签名就是 §6.2 的 ABI）：

1. 每个平台起**一个专用 drain 线程**，循环 `nexcore_drain(h, -1, &n)`；这是唯一允许调用 sink 的线程。
2. sink 内只做拷贝与投递（切到 UI 线程），不同步调用 JS/Core，不调用 `shutdown/free`。
3. UI 线程只调用 `nexcore_send`（以及生成的绑定），不直接触碰 `JS_*`。
4. 生命周期固定为：`create` → 起 drain 线程 → `set_sink` → 运行 → 停 drain（`shutdown` 唤醒）→ `shutdown` → `free`。
5. `drain` 返回 `NX_E_SHUTDOWN` 时线程退出；返回 `NX_E_INTERNAL` 时按 FATAL 处理并重建。

**transport 最小接口（P1-c：宿主到底要提供什么）**

结论先说：**宿主只提供两个可选回调（`now_ms`、`log`）和一个必需的 sink；线程创建/join/destroy 与 drain 唤醒都不是宿主接口，由框架内部 shim 提供。**

| 能力 | 提供方 | 签名 | 调用线程 | 可否为 NULL | 不可用/失败时 |
|---|---|---|---|---|---|
| 单调时钟 | 宿主，可选（`struct NexCoreConfig` 尾字段） | `int64_t (*now_ms)(void *user)`，单位 ms | JS 线程（超时、turn 预算、stat 时间戳）；drain 线程只把等待时长换算成 ms | 是；NULL 时用 `clock_gettime(CLOCK_MONOTONIC)` | `create` 时采样两次：非单调、返回 ≤0、或回调内阻塞 → `NX_E_INVALID_ARGUMENT`；运行期不得回退 |
| 日志输出 | 宿主，可选（`struct NexCoreConfig` 尾字段） | `void (*log)(void *user, int32_t level, const char *msg, size_t len)`；`level` 0=debug/1=info/2=warn/3=error；`msg` 仅回调期间有效，`len` 不含结尾 NUL | 任意 Bridge 线程（JS 线程、drain 线程、调用线程） | 是；NULL 时静默 | 回调内调用 `nexcore_*`、阻塞或抛异常属宿主 bug；debug 构建断言，release 不检测 |
| sink（消息出口） | 宿主，必需（`nexcore_set_sink`，不是 Config 字段） | `void (*NexCoreSink)(void *user, const char *json, size_t len)` | 仅 drain 线程 | 否 | 未设置就调用 `drain` → `NX_E_INVALID_ARGUMENT`，且不消费 outbox；`sink == NULL` 等价于未设置 |
| 线程 create/join/destroy（JS 线程） | **框架内部 shim**，不是宿主接口 | `void *nx_thread_create(NxThreadFn fn, void *arg)`；`int32_t nx_thread_join(void *t)`；`void nx_thread_destroy(void *t)` | create 在调用 `nexcore_create` 的线程；join 在 `shutdown`/`free` 的线程 | 不可为 NULL | pthread 失败 → `NX_E_INTERNAL`。宿主**不创建 JS 线程**，也不需要提供线程原语 |
| 线程 create/destroy（drain 线程） | 宿主，用平台原生 API | 平台自选（如 `pthread_create` / `Thread` / `Executor`），但必须是**一个专用线程**并阻塞在 `nexcore_drain` | 宿主 | 不可为 NULL（必须有） | 创建失败由宿主处理；线程必须在 `drain` 返回 `NX_E_SHUTDOWN` 后才退出，且不得被 UI 线程复用 |
| drain 唤醒的写端 | **框架内部**，无宿主接口 | 无写端；内部 `mutex + condvar`，`nexcore_send`/JS 线程在锁外 `cond_signal` | signal 由 `nexcore_send` 调用线程或 JS 线程发出 | 不适用 | 宿主**不提供** pipe/eventfd/socket/kqueue；也不要把平台事件循环塞进 drain 线程 |

补充约束：

- 上表之外的任何"宿主回调"都不存在。宿主不要为线程唤醒、时钟推进或日志格式化暴露额外函数；多一个回调就多一条跨语言线程约束。
- `now_ms` 与 `log` 都通过 `struct NexCoreConfig` 传入；`struct_size` 只到 `bootstrap_len` 的旧调用方视为未提供这两个尾字段，库不得越界读取。
- 平台侧必须保证：`sink`、`log` 里不同步调用 `nexcore_send`（会引入额外线程与锁顺序）、不调用 `shutdown/free`（`NX_E_REENTRANT`）。

### 1.4 框架对宿主的假设

1. 宿主提供一个可被 C 调用的业务 core，并按生成的 registry 注册 `c_handler`；框架不规定 core 的语言，只规定 ABI 形状。
2. 宿主提供一段随包内置的业务 JS（`bootstrap_js` + app 脚本），只通过生成的 `globalThis.nexcore` 访问外界。
3. 宿主的业务数据能表达为 JSON；大二进制走 Base64 或另开通道（Q-04）。
4. 宿主 UI 每个平台实现一个 transport：阻塞在 `nexcore_drain`，把消息切到 UI 线程，并负责生命周期。
5. 宿主的 Schema 内容由宿主定义和维护；框架只校验形态与生成规则，不评判业务含义。

### 1.5 示例 Demo 的定位

仓库内的 macOS/Android 示例 app 只用于**验证框架可用**（transport、线程、关闭、协议往返），**不是框架交付物**，也不构成产品第一版。示例里的业务 core、业务 JS、UI 与 Schema 都是宿主内容的替身，验收时以框架断言为准（见 acceptance.md §0 的分层）。

## 2. 系统上下文

```
┌────────────── 宿主项目 ──────────────┐          ┌────────── 宿主项目 ──────────┐
│ 原生 UI（SwiftUI / Compose / 平台能力）│          │ 业务 JS（编排 / 胶水 / 状态）  │
│ 只依赖框架生成的绑定与 transport      │          │ 只能看到 globalThis.nexcore   │
└───────────────┬──────────────────────┘          └──────────────┬────────────────┘
                │ JSON 信封（UTF-8）                              │ 生成的受限 API
                │ 请求 / 响应 / 错误 / 通知                         │
┌───────────────▼─────────────────────────────────────────────────▼────────────────┐
│                          ct-NexCore 框架（平台无关）                              │
│  ┌─────────────┐ ┌──────────────┐ ┌────────────────────┐ ┌──────────────────┐   │
│  │ C ABI shell │ │ 队列/线程/时钟 │ │ 协议分发与 pending  │ │ QuickJS 运行时宿主 │   │
│  └─────────────┘ └──────────────┘ └────────────────────┘ └──────────────────┘   │
│  ┌──────────────────────────────┐ ┌──────────────────────────────────────────┐  │
│  │ 代码生成器 CLI（生成物在宿主侧）│ │ 结构化调试日志（错误码/统计/trace）        │  │
│  └──────────────────────────────┘ └──────────────────────────────────────────┘  │
└───────────────▲──────────────────────────────────────────────────────────────────┘
                │ 生成的 c_handler 注册（直接 C 调用，同线程，非 JSON）
┌───────────────┴──────────────┐
│ 宿主业务 core（CT/C 等实现）   │
└──────────────────────────────┘
```

边界读法：

- 框架**只夹在中间**，内部没有 UI 层，也没有业务 JS 层；左右两块都是宿主项目。
- 宿主业务 core 通过生成的 registry 注册到框架，框架只负责按方法名分发，不解释业务含义。
- 框架交付生成器与生成规则；生成物落在宿主项目里，供宿主 UI/JS/Core 使用。

## 3. 分层与依赖方向

### 3.1 参与方职责与归属（框架内部只有 Bridge 一层）

下表只有 **Bridge** 属于框架；UI、Core、JS 是宿主侧参与方，列在此处只为说明边界与禁止项，不是框架的组成部分。

| 层 | 归属 | 职责 | 不负责 |
|---|---|---|---|
| UI | **宿主** | 渲染、输入、平台能力、权限、生命周期；使用框架生成的绑定与 transport | 业务规则、协议实现、线程调度 |
| Bridge | **框架** | C ABI、线程与队列、协议分发、超时/取消、QuickJS 运行时宿主、生命周期、限额、结构化日志 | 具体业务规则、平台 API、UI 状态 |
| Core | **宿主** | 业务计算、领域状态机、业务规则；通过生成的 registry 注册 `c_handler` | 直接调用 `nexcore_*`、持有 `JSContext*`、定义协议 |
| JS | **宿主** | 业务编排、动态规则、轻量状态、调用 core/ui | 直接访问平台、直接调用 `nexcore_*` 原始 ABI |
| Generated | **框架工具链产出，落宿主仓库** | 由宿主 Schema 派生的类型、协议常量、注册表、绑定 | 任何手写业务逻辑 |

框架的职责边界止于“把调用送到、把结果送回、把错误报清”；业务层内部做什么不在框架内。校验、状态机、计价、流程编排都属于宿主 Core 或宿主 JS。

### 3.2 依赖矩阵

| 从 \ 到 | 宿主 UI | 框架 Bridge | 宿主 Core | 宿主 JS | 生成物 | 平台 SDK |
|---|---|---|---|---|---|---|
| 宿主 UI | — | 仅生成的 transport | 禁止 | 禁止 | 允许 | 允许 |
| 框架 Bridge | 禁止 | — | 仅生成的 registry/`c_handler` | 仅 QuickJS 运行时 | 仅 C 注册表 | 禁止 |
| 宿主 Core | 禁止 | 禁止 | — | 禁止 | 仅类型 | 禁止 |
| 宿主 JS | 仅 `nexcore` API | 仅 `nexcore` API | 仅生成的 core 命名空间 | — | 允许 | 禁止 |
| 生成物 | 禁止 | 仅调用 ABI | 仅调用显式 `c_handler` | 仅调用 `nexcore` | — | 禁止 |

### 3.3 禁止跨层的具体行为

1. 宿主 UI 不得 `#include`/`import` QuickJS、Bridge 内部头文件、Core 头文件；只能使用 `generated/` + `platform/transport`。
2. 宿主 Core 不得调用 `nexcore_send`、不得持有 `JSContext*`、不得知道协议信封的存在。
3. 宿主 JS 不得直接调用 `__nx_*` 内部函数；只能使用生成的 `nexcore` 命名空间。
4. Generated 代码不得调用平台 API（`DispatchQueue`、`Handler`、`JNIEnv`）。
5. 除 Bridge 的 QuickJS 绑定单元外，任何 `.ct` 不得 `#include "quickjs.h"`。
6. 除 `src/NexCore.h.ct` 外，任何单元不得导出 `nexcore_*` 符号。
7. 平台条件编译只能出现在 `.shim.h`/`.shim.c`，不得出现在 `.ct`（见 FIX-15 / GAP-04）。
8. 框架不得实现业务语义：Bridge 中不得出现业务校验、状态机、计价、流程分支；这些属于宿主 Core/JS。**这是人工评审规则**（与 GEN-10/GEN-14 同类），`check-boundaries.sh` 只做结构与符号检查，不冒充能自动识别“业务语义”。

### 3.4 Bridge 单元拓扑（多单元 + 映射单元）

**选择 (b′)：保留多单元，用生成的 `<ns>_map.m.ct` 解决跨单元调用。** 不采用单单元，也不采用根协调转发 (b)。

先摆事实（`/tmp/ctmap` 最小工程实测，A、B 都是命名空间单元，根单元只作构建入口）：

- A→B 与 B→A 在同一次构建里编译、链接、运行全部通过；
- `a/a.h.ct` 的 `b::X.call(x) + 1` → `out/a.c` 的 `(b_bfn((x)) + 1)`；
- `b/b.h.ct` 的 `a::Y.call(x)` → `out/b.c` 的 `(a_abase((x)))`；
- include 由映射单元 prologue 带入：`out/a.c` 顶部有 `#include "b.h"`，反之亦然；
- `cc -std=c99 -Wall -Wextra -Werror` 全过，运行通过；
- 无 import 环：根 → a,b → 映射单元；映射单元不 import 实现单元；C 头只有原型、互不 include。

因此此前“命名空间单元无法跨单元调用”的结论作废，GAP-13 按“无映射单元时不可调用”重新表述。

落地形态（**命名空间路径即目录路径，不能平铺**）：

```
src/
  NexCore.h.ct              # 根单元（无 namespace）：ABI 入口与构建入口
  NexCore.ct                # 根实现：import 各命名空间的 map 单元
  js_host/
    js_host.h.ct            # namespace js_host
    js_host.ct              # 自身 import 写 `import js_host;`
    js_host_map.m.ct        # 生成：prologue include + 静态 trait + cname recipe
  queue/
    queue.h.ct              # namespace queue
    queue.ct
    queue_map.m.ct
  protocol/
    protocol.h.ct           # namespace protocol
    protocol.ct
```

- **目录规则**：`namespace js_host;` 只能出现在 `src/js_host/` 下，不能写成平铺的 `src/js_host.h.ct`。构建统一带 `--ns-root src`，由 CT 的 `check_ns_dir` 校验目录与命名空间一致（实测：不一致即拒绝）。
- 消费者单元写 `import <ns>::<ns>_map;`；跨单元调用写成 `<ns>::<Trait>.<method>`，由映射单元把它落到 cname。
- import 分三类，不再合并成一句：
  1. **允许且强制**：自身 `.h.ct`——写成**单段 base 名**，`js_host.ct` 写 `import js_host;`，根单元 `NexCore.ct` 写 `import NexCore;`；解析到同目录 `<base>.h.ct`。这是同一单元的声明可见性，CT 没有 head/impl 自动配对机制。**每个实现 `.ct` 都必须有这一行**：漏掉时 CT 会 `exit 0` 静默通过，但产物里没有 `<base>.h`、函数丢掉命名空间前缀，直到下游 `cc` 才 fatal error，因此必须由脚本显式检查。
  2. **允许**：`<ns>_map.m.ct` 映射单元（`import queue::queue_map;`）。
  3. **禁止**：其他实现单元 `.ct`（它本来也不能被 import，见 GAP-01）。
- 映射单元（`*_map.m.ct`）**同样受白名单约束**，不能整类跳过：它**不得 import 任何实现单元**（包括自己的 `<ns>`），声明由 prologue `#include "<ns>.h"` 提供；唯一允许的 import 是别的 `<ns>::<ns>_map`。在 `queue_map.m.ct` 里写 `import queue;` 必须被拒绝。
- 映射单元不含实现，只有 prologue include、静态 trait 与 cname recipe；生成物提交并逐字节校验。

**为什么选 b′ 而不是 b（根转发）**——两者都能编译。只保留真正成立的差异；不成立的维度明确不拿来论证：

1. **可机械校验性（成立）**：b′ 的跨单元契约是**声明式**的“命名空间 →（trait, method）→ cname recipe”，生成器可以按符号表逐条断言（三处一致、无重复 key、无残留 cname）；b 的转发是**过程式**的注册/注入，检查同样可以做，但需要额外定义注入时机、存储位置与初始化顺序的断言，规则面更大。注意：这是**规则面大小**的差异，不是“b 做不到”。
2. **AI 生成友好度（部分成立）**：b′ 的调用侧形态统一为 `<ns>::<Trait>.<method>`，生成器只做一次改写；b 需要同时生成调用与注册两侧，任何一侧漏掉都表现为链接失败。**回调侧两方案相同**：映射与转发都不能取 `&ns::fn`，都必须在调用方生成 `static` wrapper，因此“调用形态完全统一”不成立。

**明确不构成差异的维度**（上一版据此判 b 更差，已撤回）：

- **改动处数**：b 要改“调用点 + 根注册”，b′ 要改“调用点 + 目标 map recipe”，都是两处；差异只是改在中心还是改在目标本地，不构成优劣。
- **调试栈深度**：b 的转发可以生成成 `static inline` 或被内联，没有实测证据表明它必然多一层；“b 多一层”不作为论据。
- **耦合方向**：b 是中心式、b′ 是点对点，这是拓扑偏好，不是可判定优劣；两者都可行。

**代价**：

1. 每个被调命名空间多一个生成文件；构建清单与漂移检查要覆盖 `*_map.m.ct`。
2. 每个目标命名空间要维护一组静态 trait/recipe，生成器符号表要能产出 recipe。
3. **回调问题不消失**：映射不能取 `&ns::fn`；回调仍需在调用方单元生成 `static` wrapper（GAP-12 / D-14 / GEN-16）。
4. 映射单元是生成物，评审时需同时看“生成器规则是否对”和“生成物是否逐行可读”。
5. **接口变更要同步三处**：目标单元签名、`<ns>_map.m.ct` 的 recipe、调用点。同步规则：recipe 的 `(ns, trait, method, cname, 签名)` 必须与目标导出头和调用点完全一致。检查分两层——生成器内部断言三处一致（AST 级，不是外部 grep），外部由 `cc -Werror=implicit-function-declaration` 兜住遗漏的 cname。
6. **多 trait / 同名方法**：recipe key 固定为 `(ns, trait, method)`，同一命名空间内两个 trait 可以有同名方法，由 trait 限定名消歧；若两个 recipe 落到同一 cname 但签名不同，生成器报错；若签名相同，允许共享 cname。一个消费者 import 多个 map 时，trait 名冲突按 `(ns, trait)` 判断，不按裸 trait 名。
7. **map 被误当实现单元 import**：`.m.ct` 后缀只是命名约定，不足以防护。生成器保留 `_map` 后缀（实现单元不得以 `_map` 结尾），并检查 map 单元不含函数定义；若误把 map 当实现导入，编译可能通过（只有原型），但**链接会因缺少 cname 定义而失败**，由链接门禁和 `nm` 符号检查发现。
8. **stale map 与头名冲突**：删除/重命名命名空间后，孤儿 `*_map.m.ct` 由 `--check` 的“多余生成物”检查发现并报错（不只比对已有文件内容）；map 的 prologue include 使用**带目录的相对路径**（如 `#include "a/b.h"`），生成器在符号表里检测头文件 basename 冲突（`a` 与 `a::b` 同时平铺时直接拒绝），不做“靠文件名碰运气”。

**被否决的方案**：

- (a) 单单元：可编译，但把 ABI、协议、QuickJS、队列塞进一个文件，违反“固定模式可逐条审查”。
- (b) 根协调转发：可行（D-15 保留为备选），但跨单元规则面更大（需额外定义注入/初始化顺序），调试栈优势未证实；若将来 CT 的 import 语法或 map 机制变化导致 b′ 不可用，再退回 b。
- `#ct-raw` 手写 `extern` + cname：**已实测可用**（raw 内声明的函数可调用，`/tmp/ctm6`），但因把 C 符号与平台细节带入 CT、破坏“无魔法”而**主动不采用**；这是试过并放弃，不是没试过。

### 3.5 为什么宿主 UI 不直接调用宿主 Core

- 保持**唯一协议入口**：所有跨端调用都经过宿主 JS，宿主 UI 不需要理解宿主 Core 的领域模型。
- 让宿主 JS 可以在调用 Core 前后插入校验、缓存、埋点、降级，不需要改 UI。
- 让宿主 Core 不感知调用方是 UI 还是 JS，也不需要直接依赖框架 ABI。
- 代价：UI→Core 多一次线程跳转与 Promise。若后续性能证明不可接受，再评估受控的 fast path（Q-18），但不在 v1。

## 4. 固定模式：桥接层到底固定在哪

以下 16 条是 Bridge 的结构性约束。每条都给出**检查方式**，避免只停留在形容词。

| ID | 约束 | 检查方式 |
|---|---|---|
| FIX-01 | 只有一个 ABI 单元 `src/NexCore.h.ct`；只导出 7 个 `nexcore_*` 函数 | `nm -g` 白名单比对；`grep -rn "pub .*nexcore_" src/` 只允许该文件 |
| FIX-02 | ABI 单元不写 `namespace`，保持 C 符号名稳定；其他单元可用 namespace | `grep -n "^namespace" src/NexCore.h.ct` 必须为空 |
| FIX-03 | 跨 ABI 只用 C 标量、指针、`pub typedef`、无 `use Trait` 的 `pub struct` | 头文件审查 + 禁止 `use` 出现在 ABI 类型中 |
| FIX-04 | 每个 handle 恰好一个 JS runtime、一个 JS 线程、一个 drain 线程 | 运行时断言线程 ID；TSAN 测试 |
| FIX-05 | 所有 ABI 字符串是 UTF-8 且带长度；回调内 JSON 以 NUL 结尾但仅回调期间有效 | 代码审查 + 单元测试（嵌入 NUL、非 UTF-8） |
| FIX-06 | 除 JS 线程外任何线程不得调用 `JS_*`；`JS_UpdateStackTop` 必须在新线程入口调用 | 线程断言包装器；`grep` 禁止直接 `JS_` 出现在非绑定单元 |
| FIX-07 | 用户回调（sink）只能从 `nexcore_drain` 内调用，且只由一个 drain 线程调用 | 断言 + 文档 + TSAN |
| FIX-08 | Bridge 不包含任何平台框架头；平台差异只在 shim | `grep -rn "Foundation\|UIKit\|android/\|jni.h" src/` 必须为空 |
| FIX-09 | 跨线程数据一律复制进 Bridge 自己的缓冲区；调用方内存不被保留 | 所有权测试：调用后立即 `memset` 调用方缓冲区 |
| FIX-10 | CT 中不写手写 JSON 解析器；JSON 的 parse/stringify 只走 QuickJS | `grep` 禁止 `json_parse`/自制词法分析；所有解析调用点集中在绑定单元 |
| FIX-11 | 除 sink 外不保存任何 native 函数指针；不在 JS 线程调用 native 回调 | 代码审查 + 回调线程断言 |
| FIX-12 | JS 线程不阻塞等待 UI；drain 线程不阻塞等待 JS（`shutdown` 的 join 除外） | 锁顺序审查 + 超时测试 |
| FIX-13 | 生命周期两阶段：`shutdown` 后 `free`；`free` 后不得再调用任何 ABI | 状态机测试 + ASAN |
| FIX-14 | 生产环境不做运行时代码生成、`eval`、`new Function`、动态 `import()` | `grep` 生成物与 JS 入口 |
| FIX-15 | `.ct` 中不出现 `#ifdef/#ifndef/#if`；平台分支在 shim 内 | `grep -rn "#if" src/ --include=*.ct` 必须为空 |
| FIX-16 | **Bridge 单元拓扑固定**：每个实现 `.ct` 必须以 `import <self>;`（单段 base，如 `import js_host;`）开头；其余 import 只允许 `<ns>::<ns>_map`，禁止其他实现单元（含 `<ns>::<ns>` 这种头单元）。映射单元（`*_map.m.ct`）同样不跳过：不得 import 任何实现单元（含自己的 `<ns>`），唯一允许的是 `<ns>::<ns>_map`。跨单元调用一律写成 `<ns>::<Trait>.<method>` | `grep -rn "^import" src/ --include=*.ct` 逐行解析：实现单元缺 self import 即失败（CT 会静默丢前缀）；实现单元的 import 只能是**自身** `.h.ct`（且必须是第一条）或已存在的 `<ns>/<ns>_map.m.ct`；映射单元出现任何单段 import 即失败；每个生成 `.c` 过 `cc -Werror=implicit-function-declaration -fsyntax-only`；生成 C 中不得出现 `::` |

> FIX-10 的含义：`nexcore_send` 只把字符串入队；真正的 JSON 解析发生在 JS 线程，用 `JS_ParseJSON`。UI→JS 的参数因此天然是 `JSValue`，Core 包装函数直接用 `JS_ToInt32`/`JS_GetPropertyStr` 读取，不需要 C JSON 解析器。

## 5. CT 能力核查

本节回答方案成立的前提。结论来自本机 `ct`（`c27da3d`）实测与源码；探针源码只放在 `/tmp`，未写入本仓库。

### 5.1 C ABI：可以导出，但有边界

实测：在 `NexCore.h.ct` 中声明

```c
pub typedef void *NexCoreHandle;
pub NexCoreHandle nexcore_create(void);
pub int nexcore_dispatch(NexCoreHandle h, const char *json);
```

执行 `ct build --emit-c NexCore.h.ct -o out` 后：

- `out/NexCore.h` 包含 `typedef void *NexCoreHandle;` 与三个函数原型；
- `out/NexCore.c` 包含同名函数定义；
- 符号是普通 C external linkage，无 namespace 前缀（因为 ABI 单元不写 `namespace`）。

结论：**CT 可以导出稳定的 C ABI**，但成立条件很具体：

1. **只有 `ct build --emit-c` 会生成完整头文件**。单文件 `ct compile` 产出的头是空壳，且 `ct compile` 模式完全不接受 `import`；因此本项目固定用两趟 `ct build --emit-c --ns-root src`：Pass 1 只编 `src/NexCore.h.ct` 作为唯一发布头，Pass 2 带 `-I src` 编全部实现 `.ct`（见 §5.5、§6.1）。普通 `.ct` 可以作为**导入方**在 `ct build` 模式正常 import；作为**被导入方**必须是 `.h.ct` / `.m.ct`。
2. `pub` 是导出开关；`typedef` 也必须 `pub`，否则头文件里会引用一个未定义的类型（实测：不加 `pub` 时头文件只有函数原型，没有 typedef）。
3. `pub struct X {...}` 导出字段布局；`pub struct X;`（只有分号）是**正确的不透明声明写法**，头里只有 tag；非 `pub` 的 struct 也只在头里得到前置声明。句柄本身用 `pub typedef void *NexCoreHandle`，不导出内部布局。
4. 没有可见性属性、符号版本、调用约定标注；跨平台导出需要平台侧自己控制（`-fvisibility=hidden` + 显式导出列表）。
5. 没有 C++ ABI；只能走 C。

### 5.2 函数指针与回调：可以做 C 风格回调，不能做闭包

实测 A：

```c
pub struct Sink {
    void *user;
    void (*on_result)(void *user, int id, const char *json);
};
pub int call_sink(struct Sink *s, int id, const char *json) {
    s->on_result(s->user, id, json);
    return 0;
}
```

C 侧给 `Sink` 填函数指针后调用，输出 `cb id=7 json=hello`，退出码 0。

实测 B：

```c
extern void register_cb(void (*cb)(int));
static void my_cb(int x) { (void)x; }
pub void install(void) { register_cb(my_cb); }
```

C 侧保存并在稍后调用该指针，退出码 0。

结论：

- 函数指针可以作为结构体字段、参数、全局变量、typedef；**在同一单元内**，CT 函数名作为右值可直接赋给函数指针。
- **跨单元取地址会静默生成坏代码**：`Fn p = &f;`（`f` 来自 import）退出码 0、零诊断，生成的 C 是 `Fn p = (&f);`，随后 `cc` 报 `use of undeclared identifier 'f'`。因此跨单元回调只能在调用方生成 `static` wrapper 再取地址（GAP-12、D-14）。
- **跨单元调用需要映射单元**：无映射时三条路都不通——裸名 `f(v)` exit 0、发射 `f(v)`、不生成 `#include`，`cc` 报未声明；限定名 `s::k(v)` CT 报 `no function of that name in that namespace`；手写 cname `s_k(v)` 同样缺声明。加入生成的 `<ns>_map.m.ct` 后，A↔B 双向编译、链接、运行全部通过，include 由 prologue 带入（GAP-13、FIX-16）。映射单元只解决**调用**，不解决**取地址**：`&ns::fn` 仍不可用，回调仍需本地 `static` wrapper（GAP-12）。该问题只影响函数，不影响变量：`&g_var` 会正确生成 `&a_b_mod_g_var`。
- 没有闭包/捕获；上下文必须显式用 `void *user` 传递。
- 不能返回函数指针；不能嵌套函数指针；不能把函数指针数组作为参数（CT 绑定规则的限制）。
- **限定名只能出现在调用位置**：`ns::f(1)` 可以；`run(ns::f, 2)`、`= ns::f`、`&ns::f` 全部失败，报 `no variable of that name in that namespace`；函数也不支持别名导入。因此“把某个命名空间函数注册为回调”不能直接取地址，**生成器必须为每个回调生成显式 wrapper**（见 §5.6 GAP-05）。
- QuickJS 的 C 回调签名是固定的 `JSValue (*)(JSContext*, JSValueConst, int, JSValueConst*)`，必须用**普通 C 函数**承接；不能用 trait 对象或带方法的接口。
- 回调跨越 ABI 时，必须是 `extern "C"` 语义的普通函数；不要传 `use Trait` 的结构体。

### 5.3 结构体与 trait 的实际语义

实测（`ct build --emit-c` 导出的头文件）：

| 定义 | `sizeof` | 原因 |
|---|---|---|
| `struct Person { char *name; int age; }` | 16 | 指针 + int + padding |
| `struct Person { ... } use Greeter` | 24 | 尾部追加 `struct Greeter_vtbl *__greeter_vptr` |
| `struct Pair { int x; char y; } use First, Second` | 24 | 两个 trait = 两个 vptr |
| `struct Holder { int tag; One inner; int64_t tail; }`，`One` 带 trait | 40 | 按值嵌套会**传染** vptr 与 padding |
| 同上，但 `One *inner` | 24 | 指针嵌套不传染 |

关键事实：

- `struct` 的字段体按原文输出到 C；没有 trait 时布局就是 C 布局。
- `use Trait` 会在尾部追加隐藏 vptr，**并且 vptr 会出现在生成的头文件里**。Swift 按头文件算偏移能对上，但只要结构体后来加了一个 trait，尾部和 `sizeof` 立刻变化，已编译的 Swift 二进制就错位。
- 按值嵌套会传染：包含带 trait 成员的 `Holder` 也会膨胀；改成指针嵌套则不传染。因此“某个字段是不是结构体”不足以判断风险，必须递归检查整条类型链上是否有 `use`。
- Swift 侧若自己构造带 vptr 的结构体再传回 CT，`__greeter_vptr` 是未初始化指针；CT 把它当接口值使用会崩。`ct check` 对此零诊断；在 `.ct` 源码里引用该成员还会报“结构体没有这个字段”（它只存在于生成的 C 里）。
- Trait 是 vtable + iface 的胖指针模型；方法有隐式 `self`；静态 trait 用 `Trait::Host.method(...)` 映射。它适合 CT 内部多态，不适合做 ABI 边界。
- 跨单元使用 trait 需要 `pub trait` + `pub struct`，否则 vtable 不可见；这进一步说明它不该出现在 ABI 上。

**跨语言数据形状的硬规则：**

1. **业务数据一律走 JSON**（UI↔JS 的全部请求/响应/事件），不把业务结构体按值跨语言传递。JSON 是字节流，天然不受 CT 结构体布局变化影响。
2. **跨 ABI 的 C 结构体只允许“ABI 控制结构”**：`NexCoreConfig`、统计键、返回码等，必须满足：
   - 不含 `use Trait`（生成器/评审硬性拒绝）；
   - 不含 CT 的 trait 对象、`NxStr` 等内部类型；
   - 布局冻结；`struct_size` 采用最小长度协商（更大即接受，尾部忽略）；字段增删/重排必须升 ABI 版本；
   - 在 C 侧加 `_Static_assert(sizeof/offsetof)`，在 Swift/Kotlin 侧加对应常量校验。
3. **生成器校验**：Schema 生成的 C 类型只用于 CT/Core 内部与 JSValue 编解码，不进入 ABI 头；生成器扫描所有 `pub struct` 定义，出现 `use` 立即失败。
4. **不透明句柄**用 `pub typedef void *NexCoreHandle`；不导出任何内部 `struct` 布局。

### 5.4 线程与并发：语言层没有，必须走 shim

- CT 没有 `thread`、`mutex`、`atomic`、`condvar` 关键字或标准库原语。
- 实测可以 `extern` 声明 pthread 函数并调用；但直接写 pthread 会让 `.ct` 充满平台类型与宏，且 CT 不接受 `errno`。
- 方案：线程/锁/条件变量 shim 必须挂在它所服务的映射单元上：`src/queue/queue_map.shim.h` / `queue_map.shim.c`（`<base>.shim.h` 必须与映射单元同目录同 base，CT 只把 shim 视为映射单元的一部分）。提供 12 个不透明函数：`nx_thread_create/join`、`nx_mutex_init/destroy/lock/unlock`、`nx_cond_init/destroy/wait/signal/broadcast`、`nx_now_ms`。CT 只看到 `void *` 与 `int`。
- 所有条件变量等待都带超时，避免永久阻塞掩盖死锁；`nexcore_drain(-1)` 是唯一允许的无限等待，且可被 `shutdown` 唤醒。

### 5.5 链接外部 C 库（QuickJS）

路径：

1. QuickJS 头文件通过 ABI/绑定单元的顶层 `#include "quickjs.h"` 引入（CT 把 `#include` 原样透传给 C）。
2. QuickJS 函数用 `extern` 声明，或用 `.m.ct` 的 `@map` 映射到 C 符号。
3. QuickJS 的宏、变参函数、`errno` 访问全部包进 `src/js_host/js_host_map.shim.h/.c`，CT 只调用普通函数。
4. 链接：**库产物不要依赖 `ct build` 的最终链接**。两趟构建的命令与目录口径**只在 §6.1「两趟构建」定义一次**，本节不重复；下面只列链接链路上与本节相关的步骤（前提：已按 §6.1 完成 Pass 1 / Pass 2）：

```bash
# 前提：build/abi/NexCore.h（Pass 1 发布头）与 build/impl/*.c（Pass 2 实现）已存在

# 1) 用严格参数编译实现 C + 生成器产物；-I 首位必须是 build/abi
cc -std=c99 -Wall -Wextra -Werror -fPIC \
   -I build/abi -I build/impl -I schema/generated/c \
   -I src/js_host -I src/queue -I src/protocol \
   -c build/impl/*.c schema/generated/c/nexcore_registry.c

# 2) 断言实现单元的符号真的进了对象文件（macOS 的 nm 会带前导下划线）
nm -g build/impl/*.o | grep -Eq ' T _?nexcore_create$'

# 3) 链接 QuickJS（静态库；Android 用 NDK 单独编译）
cc -shared build/impl/*.o ... -lquickjs -lm -lpthread -o libnexcore.so
```

**`.h.ct` 与 `.ct` 不自动配对（已知风险，证据已更正）**：

- `ct build` 只沿 `import` 找到 `.h.ct`；同目录同名的 `.ct` 是**另一个单元**，不会被自动带入，必须显式写在输入清单里。`roadmap.md` 的构建条目本来就是这样写的，不是新限制。
- 两个单元 base 相同，都会写 `<out>/<base>.c`。实测**两种输入顺序产物 `diff` 为空**，不存在“实现胜出/头文件胜出”的语义；但这一致性**没有文档保证**，属于 CT 的实现细节。
- 因此不依赖覆盖语义：Pass 2 只列实现 `.ct`；构建后用 `nm -g --defined-only` 断言每个实现符号是**定义**（`T`）而不是 `U`，再用 `grep -c '^int32_t nexcore_create(' build/impl/NexCore.c` 断言文件里有函数体而不是只有原型。
- 风险点不是当前 bug，而是未来 CT 改变发射顺序或改为报重复时，构建会静默用错文件；由上面的 `nm`/函数体断言兜底。

- Swift：实测 `swiftc -import-objc-header build/abi/NexCore.h` 可调用 CT 导出的符号；SPM 用 system library target + modulemap 指向同一头文件。
- Kotlin/JVM：`kotlinc` 已验证可加载 JNI 包装；Kotlin/Native **未实测**，v1 不作为验收路径。
- Android：JNI 包装只依赖 `build/abi/NexCore.h`；不要手写第二份结构体定义。

必须在 P2 复核的点（见 open-questions Q-01）：

- 选 Bellard QuickJS 还是 quickjs-ng；两者的 `JS_NewClassID` 签名、构建系统和部分 API 不同。
- `JS_NewCFunction` 在头文件中可能是宏；CT 应调用 `JS_NewCFunction2` 的 shim 包装，而不是依赖宏展开。
- `JS_ThrowTypeError` 等变参函数必须 shim 包装。
- 在非主线程创建 runtime 后必须调用 `JS_UpdateStackTop`；否则栈溢出检测可能失效。
- `JS_FreeRuntime` 前必须释放所有 `JSValue`；桥接层用 arena + 明确的 free 点保证。

### 5.6 已知缺口与规避

| # | 现象 | 对桥接层的影响 | 规避方式 | 强制检查 |
|---|---|---|---|---|
| GAP-01 | 被导入方必须是 `.h.ct`/`.m.ct`；普通 `.ct` 作为**导入方**在 `ct build` 模式正常，但 `ct compile` 模式完全不接受 `import`（源码：`Module.h.ct:271-276` 把普通 `.ct` 作为被导入方当错误命名） | 实现单元不能直接暴露接口；`ct compile` 不能参与多单元构建 | 接口放 `NexCore.h.ct`，实现放 `NexCore.ct` 并 import 接口单元；构建只用 `ct build --emit-c`，不用 `ct compile` | 目录约定 + 编译测试 + CI 只用 `ct build` |
| GAP-02 | `import` 必须写在 `namespace` 之前；依赖扫描只收集文件开头的 `import`（`Module.h.ct:256-266` 遇到第一个非 import 非预处理 token 就停） | 写在 namespace 后的 import 被静默丢弃，后续 C 编译才报未定义 | 固定文件头模板：`#include` → 全部 `import` → `namespace` → 声明 | lint 脚本检查 import 行号小于 namespace 行号 |
| GAP-03 | 语义分析不接受 `errno` 标识符 | 不能直接判断系统调用错误 | shim 提供 `int nx_errno(void)`；所有 errno 常量也用 shim 函数/常量暴露 | 编译 + `grep -n "\berrno\b" --include=*.ct` |
| GAP-04 | 不支持文件内 `#ifdef/#ifndef/#if` | 不能在 CT 里写平台分支 | 平台分支全部下沉到 `.shim.h/.c`；CT 只看到统一函数 | `grep -rn "#if" --include=*.ct` 必须为空 |
| GAP-05 | 限定名只能出现在调用位置：`ns::f(1)` 可以；`run(ns::f, 2)`、`= ns::f`、`&ns::f` 失败，报 `no variable of that name in that namespace`；函数不支持别名导入 | 不能把命名空间函数直接注册为回调 | 调用位置正常用 `ns::f(...)`；需要函数地址时，生成显式 wrapper：`static void nx_cb(void *u, ...) { ns::f(...); }`，注册 `nx_cb` | 生成器规则 GEN-16（本地 wrapper）+ 编译门禁 |
| GAP-06 | `pub typedef` 漏写时头文件引用未定义类型 | 平台侧无法编译 | ABI 类型全部 `pub`；头文件编译测试 | `cc -fsyntax-only` 头文件 |
| GAP-07 | 非 `pub struct` 只前向声明；`pub typedef struct X;` 是静默 exit 1、零输出 | 不透明类型写法容易踩空 | 不透明句柄用 `pub typedef void *NexCoreHandle`；需要公开 tag 时写 `pub struct X;`（不要写 `pub typedef struct X;`） | 头文件审查 + 编译测试 |
| GAP-08 | `ct build` 要求输出目录已存在 | CI 脚本易失败 | 构建脚本先 `mkdir -p` | CI 模板 |
| GAP-09 | `ct build` 默认 `cc` 参数很少（无 `-Wall -Werror`） | 生成 C 的问题可能漏过 | CI 单独用严格参数编译生成 C；不依赖 `ct build` 的默认编译 | CI 命令固定 |
| GAP-10 | namespace 会给 C 符号加前缀 | ABI 符号不稳定 | ABI 单元不写 namespace，函数名自带 `nexcore_` 前缀 | `nm` 白名单 |
| GAP-11 | CT 不做类型检查，类型错误由 C 编译器发现 | 生成代码可能“看起来对” | 生成 C 必须过 `-Wall -Wextra -Werror`；关键结构体加 `_Static_assert` | CI |
| GAP-12 | **跨单元 `&f` 会静默生成坏代码**：`Fn p = &f;`（`f` 来自 import）CT 退出码 0、零诊断，生成的 C 是 `Fn p = (&f);`，随后 `cc` 报 `use of undeclared identifier 'f'` | 回调注册必然踩；CT 层不报错，错误延后到 C | 只在**函数定义所在单元**取地址，或在调用方生成 `static wrapper` 再 `&wrapper`；禁止对限定名或跨单元符号取地址 | 生成的每个 `.c` 必须通过 `cc -Werror=implicit-function-declaration -fsyntax-only`；不依赖正则脚本 |
| GAP-13 | **无映射单元时，命名空间单元无法完成跨单元函数调用**：裸名 `f(v)` → exit 0、发射 `f(v)`、不生成 `#include`，`cc` 报未声明；限定名 `s::k(v)` → CT 报 `no function of that name in that namespace`；手写 cname `s_k(v)` → 同样缺声明。加入生成的 `<ns>_map.m.ct` 后，A↔B 双向编译/链接/运行均通过，include 由 map prologue 带入（`/tmp/ctmap`） | 直接决定 Bridge 能否拆成多单元 | 采用 FIX-16：为每个目标命名空间生成 `<ns>_map.m.ct`，消费者 `import <ns>::<ns>_map`，调用写成 `<ns>::<Trait>.<method>`；回调仍不能用 `&ns::fn`，必须本地 `static` wrapper（GAP-12） | 五条机械检查见 SYM-05 |
| GAP-14 | 命名空间单元的函数调用位置**完全不校验符号**：写 `zzz(v)` 也 exit 0 并原样发射；同一位置的变量引用却会报 `use of an undeclared variable` | 不能把“CT 没报错”当作正确性证据 | 所有正确性判断以生成的 C 能否通过 `cc -Werror` 为准；CT 退出码只作为必要条件 | CI 编译门禁；负例 `zzz(v)` 必须被 `cc` 拒绝 |

> GAP-02 的“静默”很容易踩：`scan_imports` 只读取文件开头的 import；`namespace` 之后的 import 不会进入依赖图，`ct build` 可能在后续 C 编译阶段才失败。所有 `.ct` 文件使用同一个头部模板。

### 5.7 机械检查需求清单（需求侧交付口径）

**归属**：`tools/check.sh` 由**编码侧唯一维护**。当前版本编号已正式重排为 `0 / ①a ①b ①c / ②a ②b ②c / ③ / ④ / ⑤a ⑤b / ⑥ / smoke`：**⑤a 是正式编号**（不再有“对应脚本第 5 项”的临时说法），**⑥ 是新增的 stat key 运行时契约**。需求侧不再持有或编辑脚本副本。本节只定义“检什么、读哪个产物、失败判据、负例怎么造”，编码侧按此落脚本；评审里不粘贴脚本实现。编号只追加、不复用；检查项失败 = 阻断合并。

**验收口径**：以编码侧真实运行得到的 `SCRIPT_RC=0` 与逐项 PASS 输出为准，且每个检查项都配一条**真失败过**的负例。本轮已由编码侧全量实跑通过（含 ⑤a/⑤b/⑥）；此前基于静态推演的「待实跑确认」标注全部撤销，依据从“静态推演”改为“真实 `SCRIPT_RC=0` + 逐项 PASS”。

**编号对应**：本文编号即脚本编号，不再有临时映射。

| 编号 | 检什么 | 读哪个产物 | 失败判据 | 负例怎么造 |
|---|---|---|---|---|
| 0 | 生成器漂移：`nexcore_gen.py --check` 重生成后逐字节比对 | `src/units.json`、`src/**/*_map.m.ct`、（P3 起）`schema/generated/**` | 退出码非 0；任一文件 diff 非空；异常退出也算失败 | 手改 `src/queue/queue_map.m.ct` 的一行 cname recipe（不动 `units.json`），`--check` 必须非 0 并打印首个差异文件 |
| ①a | Pass 1 只构建发布头：只把 `src/NexCore.h.ct` 作为输入 | `build/abi/NexCore.h` | `ct build` 退出码非 0；头文件不存在或为空 | 在 `src/NexCore.h.ct` 临时写入语法错误 → 必须非 0；或在 Pass 1 输入里追加 `src/queue/queue.ct`（回到单趟）→ ①a 仍为 0，必须由 ①b/①c 拦下 |
| ①b | 发布头洁净：头中所有 `struct <name>;` 前置声明与 `struct <name> {` 定义的 tag 集合 ⊆ {`NexCoreConfig`}（先剥离注释与字符串） | `build/abi/NexCore.h` | 出现任何非 `NexCoreConfig` 的 tag（前置声明或定义都算） | 在 `src/NexCore.h.ct` 加 `pub struct Foo;` → 必须失败并打印 `Foo`；把 `queue.ct` 加进 Pass 1 输入 → 出现 `queue_Ring` 时必须失败 |
| ①c | 前置声明 golden：从发布头抽取 `^struct <name>;$` 行，与仓库基线逐行一致（含顺序） | `build/abi/NexCore.h`、`abi/expected-forward-decls.txt` | `diff -u` 非空（多一行、少一行、顺序变化都算） | 在基线里加一行 `struct QueueFoo;` → 必须失败；在头里多发一条前置声明同理 |
| ②a | Pass 2 只列实现 `.ct`（`NexCore/queue/js_host/protocol`），根单元首位，带 `--ns-root src -I src` | `build/impl/*.c` | 退出码非 0；按**显式期望清单**断言时任一实现单元的 `.c` 缺失或为空（不依赖 `glob` 循环自然覆盖） | 去掉 `-I src` → map 单元无法解析，必须非 0；把某个实现 `.ct` 从输入删掉 → 对应 `.c` 缺失，必须失败 |
| ②b | 每个 `build/impl/*.c` 过严格 `cc`：`-std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -fsyntax-only`，`-I build/abi` 首位 | `build/impl/*.c`、`build/abi/NexCore.h`、`build/impl/NexCore.h` | 任一 error，或任一 warning（`-Werror` 转 error） | 在 `src/queue/queue.ct` 调用未声明的 `zzz(v)`（GAP-14 会 exit 0 原样发射）→ 必须失败；跨单元 `Fn p = &f;`（GAP-12）→ 必须失败 |
| ②c | ABI 导出符号：7 个 `nexcore_*` 在对象文件里是**定义** `T` 而非引用 `U`；兼容 Mach-O 前导下划线（`T _?name`） | `build/impl/*.o` 的 `nm -g --defined-only` 输出 | 任一符号缺失；或只出现在 `U` 行；或类别是 `t`（static/本地） | 把 `src/NexCore.ct` 里的 `pub fn nexcore_create` 改成非 `pub`/加 `static` → 必须失败；把 `src/NexCore.ct` 从 Pass 2 输入删掉（只剩头单元产物）→ `nexcore_create` 变 `U` 或缺失，必须失败 |
| ③ | 生成 C 中无 CT 限定名：剥掉注释与字符串字面量后不含 `::` | `build/impl/*.c` | 残留任意 `::` | 在 fixture `.c` 里注入 `ns::f()` 后运行检查 → 必须失败（用临时目录 fixture，不改仓库产物） |
| ④ | recipe 与符号表一致：每条 `(namespace, trait, method)` 的 callee == 符号表 cname == 导出头原型；无未登记 cname、无重复 key | `src/units.json`、`build/impl/*.c`、`build/impl/*.h` | 缺 trait/方法/cname；callee 与 cname 不一致；重复 key；残留未登记 cname | 把 `units.json` 的 `Ring.push` 改成不存在的 `push2` → 必须失败；在 `queue_map.m.ct` 手写错 cname（如 `queue_pus`）→ 必须失败 |
| ⑤a | 单元 import 拓扑：**实现单元**（非 `.h.ct`、非 `*_map.m.ct`）第一条 import 必须是自身单段 base；**映射单元**（`*_map.m.ct`）不得出现任何单段 import（它没有自身 `.h.ct`）；所有 import 在 `namespace` 之前；每条只能是「自身 `.h.ct`（同目录同名且存在）」或「`<ns>::<ns>_map`（`src/<ns>/<ns>_map.m.ct` 存在）」；映射单元同样不得 import 任何实现单元（含自己的 `<ns>`） | `src/**/*.ct`（按解析后的绝对路径判定，不用字符串后缀近似） | 实现单元缺 self import；映射单元出现单段 import；import 出现在 `namespace` 之后；import 其他实现单元；`a::b_map` 前缀与目录不一致；目标 map 文件不存在 | 删掉 `src/js_host/js_host.ct` 的 `import js_host;`（CT 仍 exit 0，必须由脚本拦）；在 `src/NexCore.ct` 加 `import queue;` → 必须失败；在 `src/queue/queue_map.m.ct` 加 `import queue;` → 必须失败 |
| ⑤b | `units.json` 双源一致：每个 unit 的 `source` `.h.ct` 中**所有 `pub` 函数名集合 == `methods` 集合**（两个方向都判；解析需支持跨行声明） | `src/units.json`、各 unit 的 `source` `.h.ct` | 任一方向差集非空（漏列会静默不生成 recipe，`--check` 不报） | 在 `src/queue/queue.h.ct` 新增 `pub fn peek(...)` 但不加进 `methods` → 必须失败（extra）；在 `methods` 里加一个 `.h.ct` 不存在的 `pop` → 必须失败（missing）；写成跨行的 `pub fn\npeek(` 也必须被识别 |
| ⑥ | stat key 运行时契约：用哨兵值预填 `*out` 后逐键调用 `nexcore_stat` | `tests/stat_keys.c`、`build/impl/*.c`、`build/abi/NexCore.h`，运行产物 `build/bin/stat_keys` | `10..14` 必须返回 `NX_E_INTERNAL` 且 `*out` 保持哨兵值不变；`0`/`19`/`20`/`1024` 必须返回 `NX_E_INVALID_ARGUMENT`；已实现键（`1..9`、`15..18`）必须返回 `NX_OK` 并写入值；编译/链接失败或进程退出码非 0 也算失败 | 把 10..14 的分支改成返回 `NX_OK`（或写 0）→ 必须失败；把 `19` 当普通键返回 `NX_OK` → 必须失败；删掉 `NX_STAT_RESERVED_FIRST/LAST` 区间判断、直接 `return 0` → 必须失败 |
| smoke | 跨单元链路：严格参数链接 `tests/smoke.c` + `build/impl/*.c` 并运行 | `tests/smoke.c`、`build/impl/*.c`、`build/abi/NexCore.h` | 链接失败；进程退出码非 0；无预期输出 | 删掉 `src/queue/queue_map.m.ct` 的一条 recipe → 链接出 `U` 符号，smoke 必须失败 |

**状态标注**：上表全部检查项（`0`、①a~①c、②a~②c、③、④、⑤a、⑤b、⑥、smoke）均已由编码侧**全量实跑通过**（真实 `SCRIPT_RC=0` + 逐项 PASS），每条都附有真失败过的负例。此前 ⑤a/⑤b 的「待实跑确认」已撤销；⑥ 自加入起即为实跑通过。

## 6. C ABI

### 6.1 原则

- 只暴露 C 标量、指针、`pub typedef`；不暴露 trait、`use` 结构体、QuickJS 类型。
- 句柄不透明；平台侧不知道内部布局。
- 调用方内存不被保留；所有输入立即复制。
- 错误用 `int32_t` 返回，不用 `errno`，不用异常。
- ABI 版本与协议版本分离：ABI 管函数签名/结构布局，协议管 JSON 语义。
- **业务数据不走 C 结构体**：UI↔JS 的请求/响应/事件全是 JSON；C 结构体只用于 ABI 控制（`NexCoreConfig`）和 CT/Core 内部。
- `NexCoreConfig` 是唯一的跨 ABI 结构体，硬约束：
  - 不得 `use Trait`（否则尾部 vptr 会让 Swift/Kotlin 已编译布局错位）；
  - 字段只用 C 标量、普通函数指针、`const char *`、`void *`；函数指针必须是 C 调用约定且不带捕获；
  - `struct_size` 是**最小长度协商**，不是严格相等：库要求 `struct_size >= NEXCORE_CONFIG_MIN_SIZE`，接受更大的调用方结构体并**忽略尾部未知字段**；`struct_size < NEXCORE_CONFIG_MIN_SIZE` 返回 `NX_E_INVALID_ARGUMENT`。只有删除/重排/改语义才升 `NEXCORE_ABI_VERSION`。
  - 两个方向都要成立：**新调用方 → 旧库**（struct_size 更大，旧库忽略尾部）；**旧调用方 → 新库**（struct_size 等于 MIN，新库对缺失字段取默认值）。因此新增字段**必须**有可用默认值；不能给默认值的新增字段一律升 `NEXCORE_ABI_VERSION`，不承诺 backward 兼容。
  - `NEXCORE_CONFIG_MIN_SIZE` 一旦发布就冻结；新增可选字段只追加在尾部并更新 `NEXCORE_CONFIG_CURRENT_SIZE`，不改 MIN。`NEXCORE_CONFIG_V1_SIZE` 是**兼容基线**（到 `bootstrap_len` 为止），`NEXCORE_CONFIG_CURRENT_SIZE = sizeof(struct NexCoreConfig)`（含尾部 `log/log_user`）；两者不相等是正常的，不要用 V1_SIZE 去判断当前头文件大小。
  - `reserved` 必须填 0；库不读取、不写回。未来若要复用，必须同时升 `abi_version`（因为旧库会忽略它）。
  - C 侧 `_Static_assert(NEXCORE_CONFIG_CURRENT_SIZE == sizeof(struct NexCoreConfig), ...)`，并断言 `NEXCORE_CONFIG_MIN_SIZE <= NEXCORE_CONFIG_CURRENT_SIZE`；按 32/64 位分别断言 `offsetof`；Swift/Kotlin 侧各生成一条 `sizeof`/`MemoryLayout` 断言。
  - 任何字段删除/重排/改语义都升 `NEXCORE_ABI_VERSION`；仅在尾部追加有默认值的可选字段除外（只更新 `NEXCORE_CONFIG_CURRENT_SIZE`）。
  - 绑定侧的可注入性（ACC-A-09 的前提）：
    - Swift：传 `@convention(c)` 的**全局函数**指针（不能是捕获上下文的闭包），`clock_user` 透传；
    - Kotlin/JVM：JNI 层提供 `extern "C" int64_t nx_kotlin_now(void *user)` 蹦床，create 时缓存 `jmethodID` 并把 JS 线程 `AttachCurrentThread` 一次；禁止在每次 `now_ms` 里 attach/查找方法；
    - 约束：`now_ms` 必须单调、无阻塞、无分配、可从 JS 线程调用；不满足时返回 `NX_E_INVALID_ARGUMENT`，不得在回调里 panic/抛异常；
    - 生产可用默认 `clock_gettime(CLOCK_MONOTONIC)`，注入主要给测试与平台适配。
- **类型名只能写 tag 形式（实测）**：`pub typedef struct X X;` 在 CT 里报 **`unexpected token`**（不是“重复 struct”）；只有“先 `pub struct X {...}`、后 `pub typedef struct X X;`”才报 `duplicate struct`；`pub typedef struct X;` 是**静默 exit 1、零输出**。正确的不透明声明写法是 **`pub struct X;`**（头里得到 `struct <ns>_X;`）。因此 ABI 头里不会出现 `typedef struct NexCoreConfig NexCoreConfig;`。本文档中单独出现的 `NexCoreConfig` 均指 tag 名；**C 源码必须写 `struct NexCoreConfig`**（含 `offsetof`、`_Static_assert` 与所有函数原型）。
  - Swift 绑定：`struct NexCoreConfig` 经 Clang importer 导入为 `NexCoreConfig`（Swift 侧类型名不带 `struct`）；生成的 Swift 代码只使用导入类型并填字段，**不得**再声明 `typealias` 或平行结构体。
  - Kotlin/JNI：C 侧原型、`offsetof`、局部变量一律写 `struct NexCoreConfig`；JVM 侧由生成器输出 `size` 与各字段 offset 常量，与 `NEXCORE_CONFIG_CURRENT_SIZE` 对齐并在初始化时断言。不要手写 `typedef` 别名——那会掩盖布局漂移，且 `pub typedef struct X X;` 在 CT 里直接报 `unexpected token`。
- 生成器扫描所有 `pub struct`：出现 `use` 直接失败；Schema 生成的 C 类型不得进入 ABI 头。

**发布头与内部结构前置声明：两趟构建（实测，正式方案）**

先纠正归因：`ct build --emit-c` 写进头文件的前置声明范围是**本次构建的单元集合**，不是 import 闭包。`src/NexCore.h.ct` 并不 import `queue`/`protocol`，但四单元一起构建时它的头里照样出现 `queue_Ring`、`protocol_*`、`NexCoreHandleImpl`；只传 `src/NexCore.h.ct` 单独构建，头里只剩 `struct NexCoreConfig;`。

**路径约定（唯一口径）**——其他文档（codegen / acceptance / roadmap / ai-friendly）只引用本处，不另立路径口径：

| 目录 | 归属 | 内容 | 可否对外 |
|---|---|---|---|
| `build/abi/` | Pass 1 产物 | `build/abi/NexCore.h`（唯一发布头）、`abi-layout.txt`、布局 dump 工具 | **是**，唯一被消费侧 include 的头 |
| `build/impl/` | Pass 2 产物 | 实现单元 `*.c`、编译出的 `*.o`，以及**同名脏头** `build/impl/NexCore.h` | 否；只允许作为实现编译输入，不得安装、分发、被平台/测试 include |

- 消费侧（平台、smoke、布局 dump）的 include 顺序固定为 **`-I build/abi` 首位**；`build/impl` 只用于编译实现 `*.c` 自身。
- 生成器校验命令固定带 `--verify-ct build/impl`（读 Pass 2 产物），不读 `src/` 侧任何生成物。
- `-I src` 只出现在 Pass 2 的 `ct build` 里，用于让映射单元解析；`-I src/<ns>` 仅用于 shim 头。不得用 `-I src` 代替 `-I build/abi`，也不得直接读取 `src/` 侧产物当发布头。
- **发布头与实现中间物**的目录名只允许 `build/abi` 与 `build/impl` 两种写法；出现其他目录名（如把发布头放进 `build/`、`build/out`）一律视为文档漂移，评审应纠正。库产物（`build/libnexcore.{so,dylib}`）与报告目录（`build/reports/`）不在此约束内。

因此构建按用途拆成两趟，**Pass 1 是唯一的发布头来源**：

```bash
# Pass 1：发布头。输入只有一个 .h.ct，产出的头不含任何内部 tag。
ct build --emit-c --ns-root src src/NexCore.h.ct -o build/abi

# Pass 2：实现。输入全部实现 .ct，必须带 -I src 才能解析 map 单元。
ct build --emit-c --ns-root src -I src \
  src/NexCore.ct src/queue/queue.ct src/js_host/js_host.ct src/protocol/protocol.ct \
  -o build/impl
```

- **发布物只有 `build/abi/NexCore.h`**；`build/impl/` 下会生成一份同名的脏 `NexCore.h`，属于实现中间物（见上方路径约定），不得安装或对外分发。
- 实测：`build/abi/NexCore.h` 的前置声明**只有 `struct NexCoreConfig;`**，`queue_Ring`/`protocol_*`/`NexCoreHandleImpl` 全部消失；与单趟构建的头 diff 恰好是这 4 条前置声明。`cc -std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -I build/abi tests/smoke.c build/impl/*.c` 零 warning，运行 `SMOKE OK`。
- **对外承诺恢复为“只暴露 ABI 类型与函数”**：发布头里除 `struct NexCoreConfig` 外，不得出现任何其他 struct 名（前置声明与定义都不允许）。
- `abi/expected-forward-decls.txt` 从“污染基线”改为**干净头断言**：唯一允许内容是 `struct NexCoreConfig;`。任何新增行都视为 ABI 表面变更，必须在评审中显式确认。
- **`NexCoreHandleImpl` 与内部 tag 的已知限制**：**单趟构建会有，两趟不会**。这不是“CT 无法只导出部分 struct”，而是“**同一次构建的单元集合决定前置声明范围**”。只要 ABI 头单元与实现单元分开构建，发布头就是干净的；把两者放进同一次构建就会污染发布头。
- 防回归：CI 必须使用上面的两趟命令。若有人退回单趟，`ACC-A-01` 的 golden diff 立即失败。运行期仍保留一条防御性约定：平台代码不得声明 `struct queue_*`、`struct protocol_*`、`struct NexCoreHandleImpl` 等同名 tag（历史单趟产物曾包含它们，宿主 unity build 下会撞名）。

### 6.2 函数清单（7 个）

ABI 单元的 prologue 固定 `#include <stdint.h>` 与 `#include <stddef.h>`：`int32_t`/`int64_t`/`size_t`/`offsetof` 只依赖这两个头，导出头不引入 `<stdio.h>` 等无关头。常量用 `pub enum`，不用 `#define`（原因见下方）。

```c
typedef void *NexCoreHandle;
typedef void (*NexCoreSink)(void *user, const char *json, size_t len);
typedef void (*NexCoreLog)(void *user, int32_t level, const char *msg, size_t len);

/* 没有 typedef 别名：C 侧一律写 struct NexCoreConfig */
struct NexCoreConfig {
    uint32_t struct_size;          /* 调用方填自己编译时的 sizeof(struct NexCoreConfig) */
    uint32_t abi_version;          /* 必须等于 NEXCORE_ABI_VERSION */
    uint32_t protocol_version;     /* v1 = 1 */
    uint32_t default_timeout_ms;   /* 0 表示不超时，默认 5000 */
    uint32_t max_message_bytes;    /* 默认 1 MiB */
    uint32_t max_pending;          /* 每个方向默认 1024 */
    uint32_t flags;                /* bit0: debug；其余保留，必须为 0 */
    uint32_t reserved;             /* 必须为 0；库不读取 */
    int64_t (*now_ms)(void *user); /* 可空；非空时 Bridge 所有计时只用它 */
    void *clock_user;              /* 传给 now_ms 的上下文；可空 */
    const char *schema_hash;       /* 64 位十六进制，NUL 结尾；debug 可为 NULL */
    const char *bootstrap_js;      /* UTF-8 源码 */
    size_t bootstrap_len;
    /* 以下为可选尾字段：struct_size 只到 bootstrap_len 时视为未提供，静默 */
    NexCoreLog log;                /* 可空；任意 Bridge 线程调用，必须非阻塞 */
    void *log_user;
};

/* 常量必须用 pub enum，不能用 #define。
   CT 只把 #define 写进 .c，不进导出头；enum 成员会作为 C 枚举常量出现在头文件里。
   enum 必须写在 struct 之后；V1_SIZE 用 offsetof+sizeof，是编译期整数常量表达式。 */
pub enum NxAbiConstants {
    NEXCORE_ABI_VERSION = 1,
    NX_OK = 0,
    NX_E_INVALID_ARGUMENT = 1,
    NX_E_INVALID_HANDLE = 2,
    NX_E_SHUTDOWN = 3,
    NX_E_BACKPRESSURE = 4,
    NX_E_PAYLOAD_TOO_LARGE = 5,
    NX_E_REENTRANT = 6,
    NX_E_THREAD = 7,
    NX_E_SCHEMA_MISMATCH = 8,
    NX_E_INTERNAL = 9,
    NX_E_BUSY = 10,
    NX_E_CLOSE_TIMEOUT = 11,
    NEXCORE_CONFIG_V1_SIZE = offsetof(struct NexCoreConfig, bootstrap_len) + sizeof(size_t),
    NEXCORE_CONFIG_MIN_SIZE = NEXCORE_CONFIG_V1_SIZE,
    NEXCORE_CONFIG_CURRENT_SIZE = sizeof(struct NexCoreConfig)
}

int32_t nexcore_create(const struct NexCoreConfig *cfg, NexCoreHandle *out);
int32_t nexcore_set_sink(NexCoreHandle h, void *user, NexCoreSink sink);
int32_t nexcore_send(NexCoreHandle h, const char *json, size_t len);
int32_t nexcore_drain(NexCoreHandle h, int32_t max_wait_ms, int32_t *out_count);
int32_t nexcore_shutdown(NexCoreHandle h);
int32_t nexcore_free(NexCoreHandle h);
int32_t nexcore_stat(NexCoreHandle h, int32_t key, int64_t *out);
```

**为什么常量是 `pub enum` 而不是 `#define`（实测）**：CT 的预处理器定义只落到生成的 `.c`，导出头里没有。若用 `#define NEXCORE_ABI_VERSION 1`，平台侧看不到任何常量；改成 `pub enum` 后，枚举常量在头文件里可见，且 `offsetof`/`sizeof` 可以出现在初始化表达式里。代价与边界：

- **枚举常量在 C 中是 `int`**，所以 `sizeof(NEXCORE_CONFIG_V1_SIZE)` 得到的是 `sizeof(int)`，**不能用它表达 ABI 宽度**。尺寸常量只作为“值”使用；ABI 版本/协议版本的宽度由 `uint32_t` 字段 + `_Static_assert` 承载。`NEXCORE_CONFIG_V1_SIZE` / `CURRENT_SIZE` 存储时用 `int` 常量即可（当前值 72/更大，远小于 `INT_MAX`），消费侧赋值给 `size_t` 前加 `_Static_assert(NEXCORE_CONFIG_V1_SIZE <= INT_MAX)`。
- 枚举成员的值必须能放进 `int`；本项目结构体尺寸远小于 `INT_MAX`，`offsetof + sizeof` 的结果安全。
- 不要往这里塞 64 位值或负数哨兵；新增返回码只追加、不复用编号（范围约定见 §6.3）。

**后续任何人新增 ABI 常量，都不得用 `#define`。**

说明：

- `nexcore_create` 在返回前完成 QuickJS runtime/context 创建、bootstrap 执行、schema hash 校验；失败返回非 0，`*out = NULL`。
- 时钟：`now_ms` 非空时，超时、turn 预算、stat 时间戳全部使用它；为空时用 `clock_gettime(CLOCK_MONOTONIC)`。测试注入假时钟即可复现 24h 泄漏与 p99，不依赖墙钟。
- 日志：`log` 为空时静默；非空时从任意 Bridge 线程调用，`msg` 仅在回调期间有效、`len` 不含结尾 NUL。回调内不得调用任何 `nexcore_*`、不得阻塞、不得抛异常；违反属宿主 bug，debug 构建用断言兜住，release 不检测。`struct_size` 只到 `bootstrap_len` 时视为未提供 `log/log_user`，不得越界读取。
- `nexcore_set_sink` 必须在第一次 `nexcore_drain` 之前调用；之后只允许在 drain 线程空闲时替换（由平台 transport 保证）。
- `nexcore_drain` 只能由一个线程调用；`max_wait_ms < 0` 表示无限等待，`0` 表示非阻塞，`>0` 表示最多等待。它是唯一调用 sink 的地方。
- `nexcore_shutdown` 幂等；返回后 JS 线程已退出，`nexcore_send` 返回 `NX_E_SHUTDOWN`，`nexcore_drain` 返回 `NX_E_SHUTDOWN`。关闭的步骤顺序见 [async-threading.md](async-threading.md) §7.1。
- `nexcore_free` 只能在 `shutdown` 之后（或 FATAL 之后）、且没有 drain 在阻塞时调用；否则返回 `NX_E_BUSY`。在 JS 线程内调用返回 `NX_E_REENTRANT`。
- `nexcore_stat` 用于测试/观测，键值见 §6.4。

### 6.3 ABI 返回码

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `NX_OK` | 成功 |
| 1 | `NX_E_INVALID_ARGUMENT` | 参数为空、长度越界、`struct_size` 小于最小长度、`reserved != 0` |
| 2 | `NX_E_INVALID_HANDLE` | 句柄为空或 magic 不匹配（仅未 free 时） |
| 3 | `NX_E_SHUTDOWN` | 已 shutdown/free，或正在关闭 |
| 4 | `NX_E_BACKPRESSURE` | 队列/待处理表满 |
| 5 | `NX_E_PAYLOAD_TOO_LARGE` | 超过 `max_message_bytes` |
| 6 | `NX_E_REENTRANT` | 在 sink 内调用 `shutdown/free`、在 JS 线程内调用 `free`，或重入 drain |
| 7 | `NX_E_THREAD` | 在错误线程调用（如非 JS 线程调 `JS_*`、多线程 drain） |
| 8 | `NX_E_SCHEMA_MISMATCH` | `schema_hash` 与编译进 Bridge 的不一致 |
| 9 | `NX_E_INTERNAL` | 内部错误（OOM、pthread 失败、QuickJS 创建失败）；fatal shutdown 后的首个 `drain` 返回此码 |
| 10 | `NX_E_BUSY` | `free` 时仍有 drain 在执行，或尚未 `shutdown` |
| 11 | `NX_E_CLOSE_TIMEOUT` | 关闭 turn 超过硬上限（优雅预算 500 ms + 硬上限增量 200 ms = 700 ms）被中断；已记录 `NX_STAT_CLOSE_HARD_ABORT`；句柄已进入 DEAD，可 `free` |

**返回码数值冻结**：`NX_OK=0`、`NX_E_*=1..11` 已发布，**数值不可重排、不可复用**。`0..99` 保留给框架 ABI；新增码从 `12` 起顺序追加，删除的码位空置、不得回收。枚举常量是 `int`，因此不要引入超过 `INT_MAX` 的码位。协议层错误码是字符串（见 [protocol.md](protocol.md) §4），与这里**没有数值映射**，不要按数值互转。

### 6.4 观测键

统计键与返回码同属 ABI 常量，**同样写在 `pub enum` 里**（`#define` 不会进导出头）。`nexcore_stat(h, key, out)` 的 `key` 是 `int32_t`，**数值跨 ABI，必须冻结**。

**下表是唯一分配表**（决策见 decisions.md D-20，验收判据见 acceptance ACC-A-12）。`0` 永久保持未分配；`1..18` 已分配、不改号、不重排、不复用；`10..14` 已分配但骨架尚未实现；`19` 是哨兵 `NEXT_FREE`，不可查询。`src/NexCore.h.ct` 的 `pub enum NxStatKey` 与注释表中的编号必须与本节逐项一致。

| key | 名称 | 状态 | 类型 | 含义 |
|---|---|---|---|---|
| 0 | （未分配） | 未分配 | — | 永久保留为未分配；传入 `0` 返回 `NX_E_INVALID_ARGUMENT` |
| 1 | `NX_STAT_INBOX_DEPTH` | 已实现 | int64 | 待处理 native→JS 消息数 |
| 2 | `NX_STAT_OUTBOX_DEPTH` | 已实现 | int64 | 待 drain 的 JS→native 消息数 |
| 3 | `NX_STAT_PENDING_UI2JS` | 已实现 | int64 | UI→JS 未完成请求数 |
| 4 | `NX_STAT_PENDING_JS2UI` | 已实现 | int64 | JS→UI 未完成请求数 |
| 5 | `NX_STAT_DROPPED_LATE` | 已实现 | int64 | 超时/销毁后到达的响应数 |
| 6 | `NX_STAT_MALFORMED` | 已实现 | int64 | 信封不合法（如 `result` 与 `error` 同时出现）而被丢弃的条数 |
| 7 | `NX_STAT_DROPPED_BACKPRESSURE` | 已实现 | int64 | 背压丢弃的事件数 |
| 8 | `NX_STAT_DROPPED_SHUTDOWN` | 已实现 | int64 | shutdown 时丢弃的 inbox/outbox 项数（响应 + 事件 + 未处理请求，每条 +1） |
| 9 | `NX_STAT_CLOSE_BUDGET_EXCEEDED` | 已实现 | int64 | 关闭优雅预算（500 ms）耗尽的次数 |
| 10 | `NX_STAT_CLOSE_HARD_ABORT` | 已分配未实现 | int64 | 关闭硬上限（700 ms）触发 `close_abort` 的次数 |
| 11 | `NX_STAT_JS_THREAD_ABORTED` | 已分配未实现 | int64 | join 返回但 `js_done==false`（JS 线程异常退出）的次数；同时输出 `last_turn_seq`/`last_job_id`/`last_method` 作为原因线索 |
| 12 | `NX_STAT_JS_RUNAWAY` | 已分配未实现 | int64 | `abort_requested` 导致的失控终止次数 |
| 13 | `NX_STAT_PUMP_BUDGET` | 已分配未实现 | int64 | 连续泵送超过 `pump_budget_ms`（200 ms）的次数 |
| 14 | `NX_STAT_TIMEOUT` | 已分配未实现 | int64 | pending 超时次数 |
| 15 | `NX_STAT_JS_TURNS` | 已实现 | int64 | JS 线程 turn 数 |
| 16 | `NX_STAT_JS_JOBS` | 已实现 | int64 | 泵送的微任务数 |
| 17 | `NX_STAT_JS_EXCEPTIONS` | 已实现 | int64 | JS 异常数 |
| 18 | `NX_STAT_LAST_ERROR` | 已实现 | int64 | 最近一次 ABI 错误码（取值见 §6.3） |
| 19 | `NX_STAT_NEXT_FREE` | 哨兵 | — | 下一个可用的键号，**不可查询**；传入 `19` 返回 `NX_E_INVALID_ARGUMENT` |

数值冻结与查询规则：

- **已分配编号冻结**：`1..18` 不改号、不重排、不复用；`0` 永久未分配、不得启用（启用 `0` 会让“未初始化/旧实现”与真实键值混淆）。
- **已分配未实现的 5 个键（10..14）**：`nexcore_stat` 必须返回 `NX_E_INTERNAL`，且**不得写 `*out`**。禁止返回 0（会被读成“真实观测值为 0”），也禁止当成未知 key 走 `NX_E_INVALID_ARGUMENT` 分支——它们已经分配，只是还没采样。当前实现由 `src/NexCore.ct` 的 `NX_STAT_RESERVED_FIRST/LAST` 区间判定完成。**counter 采样不单独立项**：这 5 个键对应的关闭流程、JS 线程、drain pump 在骨架里还没有事件源，现在补采样只能造值，反而破坏“已分配未实现”的语义；等子系统落地后随事件源实现，届时再把这 5 个名字加进枚举并同步注释表。
- **哨兵**：`NX_STAT_NEXT_FREE` 只用于“下一个可用号”，不可查询。新增键从 `19` 起顺序追加，追加后把 `NEXT_FREE` 顺延到新的第一个空位。
- **错误码分流**：未知 key（`>= NEXT_FREE` 或负数）返回 `NX_E_INVALID_ARGUMENT`；已分配但未实现的 key 返回 `NX_E_INTERNAL`。两者不可混用。
- **同提交同步**：新增/改动键时，`src/NexCore.h.ct` 的枚举与注释表、本节这张表、以及实现单元里的编号断言（`nexcore_stat_key_frozen`）必须在同一次提交内一起更新。
- 删除的键只把该行标记为“废弃”，编号永久空置；保留号不得提前分配。

## 7. 生命周期

**关闭顺序只在 [async-threading.md](async-threading.md) §7.1 定义一次，本节与 protocol.md §12.6 只引用，不重复步骤。**

```
create ──成功──> RUNNING ──shutdown──> STOPPING ──JS 线程退出──> DEAD ──free──> FREED
   │                │                     │
   │                └──fatal─────────────> FATAL ──JS 释放 runtime──> finish_close() ──> DEAD
   └──失败──> 释放并返回错误
```

- **RUNNING**：接受 `send`；JS 线程处理入站；drain 线程可取消息。
- **STOPPING**：`send` 立即返回 `NX_E_SHUTDOWN`；JS 线程按 async-threading §7.1 的固定顺序执行关闭 turn；`shutdown` 在返回前等待 JS 线程完成（仅 JS 线程内部发起时例外，见 §7.1 步骤 4）。
- **DEAD**：JS 线程已退出；drain 线程在下一次 `nexcore_drain` 得到 `NX_E_SHUTDOWN` 后退出；`free` 可调用。
- **FATAL**：由不可恢复错误触发（`E_JS_RUNAWAY`、QuickJS OOM、内部不变量失败）。`E_JS_RUNAWAY` 只来自 `abort_requested` 路径（脚本超 50 ms 或连续泵送超 200 ms）；优雅关闭（`stop_requested`）与关闭硬中断（`close_abort` → `NX_E_CLOSE_TIMEOUT`）不会进入 FATAL，详见 async-threading §5.2 与 §7.1。触发后：置 `stop_requested=true`，不再开始新 turn；`send` 返回 `NX_E_SHUTDOWN`；下一次 `drain` 返回 `NX_E_INTERNAL`（之后再调用返回 `NX_E_SHUTDOWN`）。**释放时机（P1-b）**：FATAL 只置标志，不得在 job 执行中途释放 runtime；只有 `JS_ExecutePendingJob` 已返回、JS 栈已展开、回到 turn 边界时，才允许 `JS_FreeContext/JS_FreeRuntime` 并置 `js_done`。随后由非 JS 线程的 `shutdown` 或 `free` 调 `finish_close()` 进入 DEAD（DEAD 仍只有这一个写点）。`free` 在 FATAL 后可直接调用：join → `finish_close()` → 销毁。
- **FREED**：句柄失效；此后任何调用都是调用方 bug（未定义行为），文档与生成绑定都必须阻止。

**`free` 与 drain 的原子性（RC-08）**：Bridge 有一把生命周期锁，`drain` 进入/退出时在锁内增减 `drain_active`；`free` 先做线程归属检查，**在 JS 线程内调用直接返回 `NX_E_REENTRANT`**（否则会 `thread_join(self)`）。然后在**同一把锁内**检查 `drain_active==0` 且已 `shutdown` 或已 FATAL（否则 `NX_E_BUSY`），若 `joined==false`，必须先 `thread_join` 再回锁 `finish_close()`；`finish_close()` 是 DEAD 的唯一写点，`js_done==false` 时记 `NX_STAT_JS_THREAD_ABORTED` 并按异常路径置 DEAD。因此不存在“检查后、销毁前 drain 进入”的窗口；若有 drain 在执行，`free` 返回 `NX_E_BUSY`，调用方必须等待 drain 返回后重试。

`shutdown` 不允许在 sink 内调用（`NX_E_REENTRANT`）。允许在 drain 线程内调用：drain 线程不是 JS 线程，`shutdown` 仍必须等待 JS 线程完成关闭 turn，不能提前返回；当前 `drain` 调用返回后 drain 线程自行退出，`free` 由另一线程在 `drain_active==0` 后执行。若在 JS 线程内部（Core handler）发起，则只置 STOPPING 并立即返回，关闭 turn 在当前 JS 栈返回后继续，见 async-threading §7.1。平台 transport 推荐在 UI 生命周期结束时先停 drain 线程，再 `shutdown/free`。

## 8. 内存与所有权

| 对象 | 所有者 | 规则 |
|---|---|---|
| `NexCoreHandle` | 平台 transport | `create` 返回；`free` 后失效；不得跨进程 |
| `bootstrap_js` / `schema_hash` | 调用方 | `create` 内复制；返回后可释放 |
| `nexcore_send` 的 JSON | 调用方 | 入队时复制；返回后可释放 |
| sink 的 JSON | Bridge | 仅回调期间有效；平台必须立即解码或复制 |
| `JSContext/JSRuntime` | Bridge | 只在 JS 线程使用；`shutdown` 时释放 |
| `JSValue` | QuickJS | 不跨线程；不跨 turn 保存（Promise handler 除外，存在 JS 堆） |
| Core 返回的字符串/数组 | Core → Bridge arena | 每次 core 调用一个 arena，返回前释放 |
| 平台 pending 的 continuation | Swift/Kotlin transport | `shutdown` 时统一以 `E_SHUTDOWN` 结束 |
| 生成代码中的常量 | 静态存储 | 只读；不动态分配 |

关键约束：

- CT 没有 GC；Core 的输出用 arena 或显式 `nx_alloc/nx_free`，生成包装函数负责释放。
- QuickJS 的 `JSValue` 必须 `JS_FreeValue`；跨函数传递用 `JS_DupValue`，规则集中在绑定层。
- 不允许把 `JSValue` 放进 C 的 pending 表；pending 表只存 id、方向、截止时间、方法名。
- 平台侧不得缓存 sink 的 `json` 指针；必须复制到 `String`/`ByteArray`。

## 9. 安全边界

1. **JS 是受限执行环境**：只能看到 `globalThis.nexcore`；没有 `require`、`fetch`、文件、线程、计时器（v1 不提供 `setTimeout`，超时由 Bridge 统一管理）。
2. **方法白名单**：C 侧 registry 是唯一允许的方法集合；JS 调用未注册方法直接得到 `E_NO_METHOD`，不进入 outbox。
3. **限额**：单条消息 1 MiB、嵌套深度 32、方法名 128 字节、每方向 pending 1024、JS 内存上限与栈上限可配置。
4. **中断**：QuickJS interrupt handler 承担“单 job 抢占”。正常运行只在 `abort_requested`（脚本超 50 ms 或连续泵送超 200 ms）时返回非 0；关闭 turn 在 grace 期（`shutdown_budget_ms`，默认 500 ms）内返回 0，超过硬上限（+200 ms）置 `close_abort` 并返回非 0，使同步死循环的 `.catch` 也能被终止，结果码为 `NX_E_CLOSE_TIMEOUT` 而非 `E_JS_RUNAWAY`。
5. **调试信息**：错误 `data.stack` 只在 `flags` 的 debug 位打开时返回；生产环境不返回堆栈与路径。
6. **密钥**：不进 JS；需要密钥的能力留在平台侧，通过窄接口暴露，且参数/返回值做脱敏。
7. **热更新**：v1 的 JS **只随 App 包内置，不从服务端下发**。这是基于 Apple App Review 2.5.2 的合规约束，不是技术限制；将来若要做下发（签名包、灰度、回滚），必须单独评审并升协议版本（Q-02 已按此前提关闭）。

## 10. 主要风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| CT 缺口（import 位置、errno、#ifdef、跨单元取地址、trait vptr） | 编译失败或静默错误 | 固定文件模板 + lint + 生成器检查 + 每周回归探针 |
| `ct build` 的 `.h.ct`/`.ct` 同名产物目前与输入顺序无关，但无文档保证 | 未来 CT 改变发射顺序/改为报重复时，构建会静默用错文件 | 显式列全部实现 `.ct` 输入 + `nm` 断言符号为 `T` + 函数体断言 + 锁定 CT 版本 |
| 发布头混入内部 struct 前置声明 | 与宿主同名 tag 冲突、ABI 表面污染 | 两趟构建（Pass 1 只编 `NexCore.h.ct`）+ `abi/expected-forward-decls.txt` 只允许 `struct NexCoreConfig;` + 平台侧命名禁令 |
| CT 不做类型检查 | 生成 C 的错误延后暴露 | `-Wall -Wextra -Werror` + `_Static_assert` + 生成物编译测试 |
| QuickJS 版本/分支差异 | 嵌入 API 不兼容 | P2 锁定版本与 fork；CI 固定 commit；shim 隔离 |
| 线程与关闭竞态 | 崩溃、use-after-free | 两阶段关闭 + TSAN/ASAN + 竞态用例（async-threading.md §8） |
| Swift/Kotlin 结构体布局漂移 | ABI 读错内存 | 生成常量 + `_Static_assert` + 三端 ABI 测试 |
| JSON 数字精度 | 金额/ID 误差 | `int32` 用 number；`int64` 限制在 ±2^53-1，超出报错；金额用最小货币单位 |
| 大 payload 性能 | 主线程卡顿、内存峰值 | 1 MiB 限额 + Base64 仅用于小二进制 + 性能验收 |
| AI 生成代码越界 | 破坏桥接层/线程假设 | 目录权限 + 脚本检查 + CI gate + 人工确认清单 |
| iOS 热更新合规 | 上架风险 | v1 不下载执行 JS（Apple 2.5.2）；将来做下发需独立评审 |
| Android JNI 复杂度 | 集成延期 | JNI 层最小化，只做 transport；生成 Kotlin 不直接碰 JNI |
| CT 许可证 | 产品合规 | 确认 `LICENSE.EXCEPTION` 覆盖生成 C；法务复核 |

## 11. 三方分工（避免互相踩目录）

| 角色 | 目录/产物 | 本文档的关系 |
|---|---|---|
| 需求（本文档） | `docs/` | 定义契约、边界、验收；不写实现 |
| 编码（框架仓库） | `src/`、`scripts/`、`docs/`、`templates/platform/` | 做 QuickJS 嵌入验证与 ABI 实现；以本文档为输入 |
| 编码（宿主仓库） | `platform/`、`js/`、`core/`、`schema/`、`schema/generated/` | 各平台 transport、业务 JS/Core、业务 Schema 与生成物；框架只提供模板与规则 |
| 评审 | 只读审查 | 核查 CT 能力边界与实现是否违反固定模式；不改文件 |

本文档定义契约与目录形态，但不提供实现代码；文中的 C/JSON 片段是契约示意，不是可直接编译的交付物。
