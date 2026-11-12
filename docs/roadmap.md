# 分阶段实现路径

四个阶段对应四个实现步骤。每阶段都给出**可验证产出**与**退出条件**；退出条件不满足不得进入下一阶段。验收条目编号对应 [acceptance.md](acceptance.md)。

**阶段边界**：每个阶段交付的都是**框架能力**（ABI、线程、QuickJS 集成、协议、生成器、日志、接入契约），不是业务功能。业务 core、业务 JS、UI、业务 Schema 内容属于宿主项目，不在任何阶段的产出内；阶段中出现的业务名词一律是 fixture/stub。

## 0. 总览

```
P1 ABI + 传输骨架
   │  产出：可编译的 C ABI、线程/队列、loopback 回显
   ▼
P2 QuickJS 嵌入 + 协议
   │  产出：单 JS 线程、请求/响应/事件、Promise、超时、关闭
   ▼
P3 Schema 处理 + 代码生成
   │  产出：YAML → C/Swift/Kotlin/JS 四端生成、漂移检查
   ▼
P4 平台接入契约 + 加固
      产出：接入契约、JNI 骨架、验证样例、压力/内存/性能验收
```

依赖关系：P1 的 ABI 与线程骨架是 P2 的基础；P2 的协议行为是 P3 生成代码的运行时；P3 的生成物是 P4 平台接入的唯一入口。任何阶段发现契约需要改，先改 `docs/`，再改代码。

## P1：ABI + 传输骨架

### 范围

- `src/NexCore.h.ct`：`pub typedef void *NexCoreHandle`、`struct NexCoreConfig`（只有 tag 名）、`pub enum` ABI 常量、7 个 ABI 函数声明。
- `src/NexCore.ct`：handle 生命周期、inbox/outbox、pthread shim 包装、状态机；**不含 QuickJS**。
- `src/queue/queue.h.ct` + `src/queue/queue.ct` + `src/queue/queue_map.m.ct`：队列与线程原语；shim 挂在映射单元上（`queue_map.shim.h/.c`），平台分支只在这里。
- 目录规则：命名空间路径即目录路径（`src/queue/`），不平铺；实现 `.ct` 自身 import 自己的 `.h.ct`。
- 构建脚本（两趟，命令与路径口径以 design.md §6.1 为唯一权威）：Pass 1 只编 `src/NexCore.h.ct` 产出 `build/abi/NexCore.h`（唯一发布头）；Pass 2 只列实现 `.ct` 产出 `build/impl`（`.h.ct` 由 self-import 自动带入；同目录 `.ct` 不自动配对，必须显式列输入）。严格 `cc -Wall -Wextra -Werror` 编译；`nm` 白名单 + 定义符号断言。
- loopback 模式：`nexcore_send` 入队，JS 线程位置先放一个 echo 消费者，把消息原样放 outbox；`nexcore_drain` 回调。
- Swift 头文件冒烟：`swiftc -import-objc-header build/abi/NexCore.h` 能编译并调用 `nexcore_create/drain`。
- Kotlin/JVM 冒烟：JNI 空壳能加载并调用 `nexcore_stat`。

### 可验证产出

| 产出 | 验证 |
|---|---|
| ABI 头 | `build/abi/NexCore.h` 含 7 个函数、typedef、config，且除 `NexCoreConfig` 外无其他 struct 名；`cc -fsyntax-only` 通过 |
| 符号表 | `nm -g` 只有白名单 `nexcore_*` + libc/pthread |
| loopback | 1000 条消息从 `send` 到 `drain` 原样返回，顺序一致 |
| 多线程 send | 8 线程 × 1000 条，全部接受或明确 `E_BACKPRESSURE`，无丢失已接受消息 |
| 两阶段关闭 | `shutdown` 后 `send` 返回 `NX_E_SHUTDOWN`；`drain` 返回 `NX_E_SHUTDOWN`；`free` 成功 |
| 严格编译 | `-Werror` 无警告；ASAN 无泄漏 |
| Swift 冒烟 | `swiftc -import-objc-header` 编译并运行最小调用 |
| Kotlin/JVM 冒烟 | JNI 加载成功，`nexcore_stat` 返回 0 |

### 退出条件

- ACC-A-01..ACC-A-03、ACC-A-05、ACC-A-06、ACC-A-08..ACC-A-10 通过；ACC-C-02、ACC-C-08、ACC-C-09（基础部分）通过。ACC-C-01/ACC-C-05/ACC-C-06 依赖 JS 线程，在 P2 关闭。
- 阶段门禁：Q-06（平台集成形态与最低版本）必须在 P1 退出前关闭。
- 没有 QuickJS 依赖也能构建；`libnexcore` 的依赖只有 libc/pthread。
- 所有 `.ct` 通过 `check-boundaries.sh`（无 `#if`、无 `errno`、import 顺序正确）。
- `NexCoreConfig` 的 `_Static_assert` 在 macOS/Android 两个工具链都通过（C 侧写 `struct NexCoreConfig`）。

### 非目标

- 不解析 JSON；不做 Promise；不跑 JS。
- 不实现 Swift/Kotlin 的生成绑定；只做手写冒烟。

### 风险

- CT 的 `pub typedef`/`pub struct` 导出细节在升级后变化 → 固定 CT commit，每周回归探针。
- pthread shim 在 Android NDK 的可用性 → P1 就在 NDK 工具链编译一次。

## P2：QuickJS 嵌入 + 协议

### 范围

- QuickJS 绑定：`src/js_host/js_host.h.ct` + `js_host.ct` + `js_host_map.m.ct`；宏/变参/`errno` 走 `js_host_map.shim.h/.c`。
- 专用 JS 线程：创建 `JSRuntime/JSContext`、`JS_UpdateStackTop`、内存/栈上限、interrupt handler。
- 协议：信封解析/序列化（`JS_ParseJSON`/`JS_JSONStringify`）、方法白名单、id/pending、超时、取消。
- Promise：JS→UI 的 Promise、UI→JS 的 dispatcher + `__nx_reply`、Core 同步调用包装。
- 事件：`$subscribe/$unsubscribe/$emit/$error/$shutdown`。
- 关闭：关闭 turn、reject pending、`JS_FreeContext/Runtime`、join。
- 测试：协议一致性、竞态 RC-01..RC-16、TSAN/ASAN、失控脚本。

### 可验证产出

| 产出 | 验证 |
|---|---|
| JS 启动 | 空 bootstrap 创建成功；`schema_hash` 不匹配时 `nexcore_create` 返回 `NX_E_SCHEMA_MISMATCH` |
| 请求/响应 | UI→JS 与 JS→UI 各 10k 次，结果正确、id 不串 |
| Promise | 异步 handler 的 `await` 后续代码在下一个 turn 前完成；10k 长度链全部执行（允许跨 continuation turn，见 ACC-C-03） |
| 超时 | 1 ms/50 ms 超时稳定；晚到响应丢弃并计数 |
| 取消 | `$cancel` 与响应竞跑 1000 次，恰好一次 settle |
| 事件 | 100k 事件保序；退订语义；handler 异常隔离 |
| 关闭 | 1000 个在途请求全部 `E_SHUTDOWN`；无 use-after-free |
| 失控 | 无限 Promise 链在预算内触发 `E_JS_RUNAWAY` 并干净关闭 |
| 线程 | 断言 JS handler 只在 JS 线程、sink 只在 drain 线程 |
| 内存 | ASAN/LSAN 无泄漏；8h 空转 RSS 稳定 |

### 退出条件

- ACC-A-11、ACC-B-01..ACC-B-08、ACC-C-01..ACC-C-07、ACC-C-11、ACC-D-01..ACC-D-04 通过。
- TSAN 无数据竞争；ASAN 无泄漏/越界。
- 在 macOS（clang）与 Android NDK（clang）各跑通一次；Linux 可选。
- QuickJS 版本/fork 锁定并记录 commit；`JS_NewClassID` 等 API 的 shim 通过编译。
- 专用 JS 线程路径复测通过（`JS_UpdateStackTop`、8 MiB 栈、interrupt handler）。

### 非目标

- 不做模块加载器/热更新；bootstrap + 单个 app 脚本足够。
- 不做多 runtime、不做远程调试。
- 不做二进制 payload（v1 只 JSON + Base64）。

### 风险

- QuickJS 版本差异 → 锁定 fork/commit，CI 固定。
- JS 线程与平台主线程约束 → 平台 API 只走 drain/UI，不进入 JS 线程。
- 微任务预算导致误杀正常长链 → 10k 是让出配额，失败只看 200 ms 连续窗口与单脚本 50 ms；验收用可重复的基准。

## P3：Schema + 代码生成

### 范围

- `schema/nexcore.schema.yaml`（宿主内容；P3 起；框架仓库只带 `tests/fixture.schema.yaml`）+ 生成器 `nexcore-gen`（P1–P2：`src/units.json` + Python 标准库 `json`；P3：Python 3 + PyYAML 锁定版本）。
- 四类产物：C registry、Swift、Kotlin、JS。
- 生成规则 GEN-01..GEN-20 与 SYM-01..SYM-05，尤其：
  - 禁止 `use Trait` 跨 ABI，递归检查；
  - 禁止跨单元 `&qualified` / `= qualified`，回调必须生成本地 wrapper（映射单元不解决取地址）；
  - 跨单元调用必须生成映射单元：`<ns>_map.m.ct` + `import <ns>::<ns>_map` + `<ns>::<Trait>.<method>`；
  - 生成器自建符号表：recipe 的 callee 必须等于符号表 cname，生成 C 中不出现 `::`；
  - 生成物做符号未定义扫描，不依赖 CT 诊断；
  - 无函数式宏、无反射、无隐式转换。
- `--check` 漂移检查、golden 测试、负例测试。
- 运行时 schema hash 三方校验（C/JS/native）。
- 构建链路固定为 `ct build --emit-c` + 严格 `cc`。

### 可验证产出

| 产出 | 验证 |
|---|---|
| 生成器 | 同输入两次字节一致；20 个负例带文件/行号报错 |
| C registry | 每个方法一个可见 wrapper；`-Werror` 通过；无函数式宏 |
| Swift | `swiftc` 编译；`@MainActor` handler；无 `Any` |
| Kotlin | `kotlinc` 编译；`@Serializable`；无反射 |
| JS | QuickJS 加载；方法名静态；无 `eval/Proxy` |
| 回调 wrapper / 单元拓扑 | 每个生成 `.c` 过 `cc -Werror=implicit-function-declaration -fsyntax-only`；跨单元取地址负例编译失败；有映射时 A↔B 正例通过；无映射的三种写法负例失败 |
| trait 检查 | 含 `use` 的 `pub struct` 负例被拒绝；按值嵌套带 trait 负例被拒绝 |
| 漂移 | 手改任一生成物 → `--check` 失败 |
| 端到端 | fixture Schema 的 echo 方法在 core/ui/js 三个方向各跑通一条；业务实现为 stub |

### 退出条件

- ACC-E-01..ACC-E-08、ACC-F-01..ACC-F-03、ACC-F-05 通过。
- 一条新增方法的完整流程（Schema → 生成 → handler → 测试）可在不改 Bridge 的情况下完成。
- 生成物在 macOS 与 Android 工具链都能编译。
- 所有生成 C 通过 `-Wall -Wextra -Werror`，关键 ABI 结构体 `_Static_assert` 通过。

### 非目标

- 不做 Schema GUI/编辑器。
- 不做运行时 Schema 解析；生成物是唯一运行时输入。
- 不做跨版本兼容层；破坏性变更升协议版本。

### 风险

- YAML 解析/生成器成为单点 → golden + 负例 + 双人评审。
- 生成物过大导致编译慢 → 按 namespace 分文件（v2），v1 先单文件。
- Kotlin/Native 未验证 → v1 不承诺。

## P4：平台接入契约 + 加固

### 范围

- 框架提供**接入契约**：transport 的 drain 线程模型、生命周期接线、`@MainActor`/Main dispatcher 调度约束；各平台 transport 由宿主实现。
- macOS/iOS：框架给出 Swift 接入骨架与 JNI/NDK 构建说明；SwiftUI 仅作为验证样例（SAMPLE-01）。
- Android：框架给出 NDK 构建 `libnexcore.so` 的约束与 JNI 骨架；Compose 仅作为验证样例（SAMPLE-02）。
- 生命周期：前台/后台、进程重建、旋转、退出；关闭顺序固定为“停 drain → shutdown → free”。
- 加固：fuzz（信封/参数）、24h soak、内存上限、性能基准、日志脱敏。
- 文档：接入指南、错误排查、Schema 变更手册（面向宿主项目）。

### 可验证产出

| 产出 | 验证 |
|---|---|
| macOS 验证样例 | 框架断言：echo 往返、后台/前台、退出无崩溃；载体 SAMPLE-01，不是交付物 |
| Android 验证样例 | 同上 + 进程重建；JNI 无泄漏；载体 SAMPLE-02，不是交付物 |
| 接入契约 | transport 接口文档 + 最小骨架可被宿主按文档接上；不依赖样例业务 |
| 同一 Schema | 两端 `schema_hash` 相同；JS bundle 字节一致 |
| 生命周期 | 1000 次 create/shutdown/free 循环无泄漏 |
| 性能 | 1 KiB payload 往返 p99 < 5 ms（参考设备，见 acceptance.md） |
| 稳定性 | 24h soak 无崩溃；RSS 增长 < 5% |
| 安全 | 超限/恶意 JSON 不崩溃；JS 无法调用未注册方法 |
| 可观测 | 错误码、统计、trace 可定位一次失败调用 |

### 退出条件

- ACC-G-01..ACC-G-06、ACC-H-01..ACC-H-05 通过。
- 平台验收设备明确（macOS 版本、Android API/ABI）。
- 关闭顺序在样例中可见且被测试覆盖。
- 没有未解决的 P0/P1 缺陷；P2 缺陷有明确豁免与期限。

### 非目标

- 不做 Linux/Windows 桌面验收。
- 不做 Kotlin/Native、不做 Web/WASM。
- 不做热更新下载执行（见 Q-02）。

### 风险

- iOS 审核对“下载执行 JS”的限制 → v1 只随包发布。
- Android JNI 线程/引用管理 → JNI 层最小化，只做 transport；每消息局部引用及时删除。
- 平台生命周期差异 → transport 模板统一，样例覆盖后台/重建。

## 跨阶段工作流

| 工作 | 频率 | 产物 |
|---|---|---|
| CT 能力回归探针 | 每周/每次 CT 升级 | `docs/` 更新 + 失败清单 |
| 生成物漂移检查 | 每 PR | CI |
| 协议一致性测试 | 每 PR | 测试报告 |
| 线程/关闭压力 | 每 PR（短）/ 每日（长） | TSAN/ASAN 报告 |
| 性能基准 | 每阶段末 | 基准数据 + 回归阈值 |
| 文档同步 | 每次契约变更 | `docs/` diff |

## 阶段与验收映射

| 阶段 | 主要验收 |
|---|---|
| P1 | ACC-A-01..ACC-A-03、ACC-A-05、ACC-A-06、ACC-A-08..ACC-A-10、ACC-C-02、ACC-C-08、ACC-C-09 |
| P2 | ACC-A-11、ACC-B-01..ACC-B-08、ACC-C-01..ACC-C-07、ACC-C-11、ACC-D-01..ACC-D-04 |
| P3 | ACC-E-01..ACC-E-08、ACC-F-01..ACC-F-03、ACC-F-05 |
| P4 | ACC-G-01..ACC-G-06、ACC-H-01..ACC-H-05、ACC-A-04、ACC-A-07、ACC-F-04 |

未列出的条目在对应阶段不阻塞，但必须在 P4 前全部关闭或明确豁免。
