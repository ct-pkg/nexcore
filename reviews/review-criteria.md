# 桥接层评审口径

| 项 | 值 |
| --- | --- |
| 适用对象 | ct-NexCore 桥接层（CT 侧 + Swift/Kotlin Host 侧 + JS 侧 Provider 约定） |
| 目的 | 在方案落地前后用同一把尺子判"能不能收"，避免临时拍脑袋 |
| 依据 | 语言事实见 [ct-capability-audit.md](ct-capability-audit.md)（下文简称"审计"） |
| 判定用语 | **P0** 阻断：必须修完才能合入 / **P1** 里程碑前必须修 / **P2** 可进待办 |

本文件只定义**可检查**的判据：每条都给出"看什么、用什么命令/测试、什么算通过、什么算不通过"。主观印象（"代码挺清晰"）不构成通过理由。

---

## 0. 术语与边界

| 术语 | 定义 |
| --- | --- |
| **JS 侧** | QuickJS 里执行的脚本与模块（Provider 的 JS 实现） |
| **Provider** | 暴露能力给 UI 的抽象接口；它是**唯一**允许出现 QuickJS 类型的层 |
| **Bridge** | Provider 的 CT 实现 + 协议编解码 + 任务队列 + Promise 泵 |
| **Host** | SwiftUI / Kotlin 原生 UI 进程内的一侧，只认识 Bridge 导出的 C ABI |
| **协议** | UI 与 JS 之间交换的请求/响应/事件的值模型与编码规则 |

目录级约定（评审时按实际目录名替换，但边界必须等价）：

```
src/provider/     ← 唯一允许 #include "quickjs.h"、出现 JS_* 符号
src/bridge/       ← 导出 C ABI、队列、泵；不得出现 JS_* 符号
src/protocol/     ← 值模型与编解码；不得出现 JS_* 符号
host/swift/       ← 只 import 生成的 C 头
host/kotlin/      ← JNI 层（只调 C ABI）
```

---

## 1. 判定门槛

| 等级 | 含义 | 处理 |
| --- | --- | --- |
| P0 | 破坏隔离、线程安全、生命周期或数据正确性 | 阻断合入 |
| P1 | 影响可靠性/可维护性，有明确触发条件 | 里程碑前必须修 |
| P2 | 风格、可读性、性能余量 | 进待办，不阻断 |

一次评审必须给出：**结论（通过 / 有条件通过 / 不通过）+ P0/P1 清单 + 证据包索引**。没有证据包的"通过"无效（见 §9）。

---

## 2. C1 分层隔离：JS 层不能绕过 Provider 直接碰原生

### 要求

1. QuickJS 头文件、`JS_*` 符号、`JSValue` 类型**只允许**出现在 `src/provider/`。
2. Bridge/Protocol/Host 看到的只有：定宽标量、不透明句柄（`uint64_t handle` 或 `void *`）、纯字段 struct。
3. Provider 的对外接口不得返回 `JSValue`、不得暴露 `JSContext *`。
4. Host 侧不得通过 `dlsym`/运行时反射去找 Provider 内部符号。

### 检查方法

```bash
# 1) 越界引用：除 provider 目录外不得出现 QuickJS 符号
grep -rn --include='*.ct' --include='*.h' --include='*.c' --include='*.shim.h' \
     -E '\bJS_[A-Za-z_]+|JSContext|JSValue' src | grep -v '^src/provider/' 
# 期望：空

# 2) 头文件污染：Bridge 公共头不得 include quickjs
grep -rn 'quickjs' src/bridge src/protocol host | grep -v '^src/provider/'
# 期望：空

# 3) 动态符号：dylib 未定义符号里 JS_* 只应来自 provider 目标文件
nm -u build/libnexcore.dylib | grep 'JS_'
# 期望：仅 provider 允许；若 Bridge/UI 目标文件引用，用 `nm -A` 定位并判 P0
```

### 通过判据

- 上述三条命令全部为空（或只剩 provider 内部）。
- 反向验证：把 Provider 的目标文件替换成 stub（所有方法返回 `NOT_SUPPORTED`），Bridge/Protocol/Host **仍能编译、链接、跑完全部协议单测**。若不能，说明有绕过。

### 不通过示例

- `src/bridge/queue.ct` 里 `#include "quickjs.h"` 只为拿 `JSValue` 做队列元素 → P0。
- Host 侧 JNI 直接 `JS_Call` → P0。

---

## 3. C2 协议对称：UI→JS 与 JS→UI 是同一套编解码

### 要求

1. **一份值模型**：类型表（JS 类型 ↔ wire tag ↔ 原生类型 ↔ 错误码）只有一个定义文件。
2. **一个编解码器**：两个方向调用同一组 `encode`/`decode`，不允许"UI→JS 用 JSON，JS→UI 用结构体"。
3. **错误对称**：同一非法输入，两个方向返回同一错误码/同一诊断字段。
4. **取消/超时对称**：两个方向都有等价的取消语义与终态。
5. 不支持的 JS 值（函数、Symbol、循环引用、BigInt 等）在两个方向都显式拒绝，不静默转 `null`。

### 检查方法

```bash
# 1) 编解码实现唯一性：encode/decode 定义点计数
grep -rn -E '\b(encode|decode)_[a-z_]+\s*\(' src/protocol | grep -v '_test'
# 期望：每个方向各自的实现只允许一处；出现两份"平行实现"即人工复核

# 2) 值模型单点：类型表只在一个文件定义
grep -rn 'TYPE_TABLE\|type_map\|ValueTag' src | sort
# 期望：定义 1 处，其余为引用
```

测试（必须存在）：

- **往返测试**：对类型表里每个类型 × 边界值（`0`、`-1`、`INT32_MIN/MAX`、`INT64_MIN/MAX`、空串、超长串、`null`、`undefined`、空数组、嵌套 3 层）做 `UI→wire→JS→wire→UI`，断言字节级相等。
- **对称性测试**：同一组非法输入分别从两个方向进入，断言错误码相同。
- **拒绝表**：把不支持的 JS 值逐个喂入，断言返回 `UNSUPPORTED_TYPE` 而不是崩溃/静默转换。

### 通过判据

- 类型表 100% 被往返测试覆盖（脚本比对类型表条目数与测试用例数）。
- 两个方向的错误码集合完全相等（除方向特有错误，且必须在文档中列出）。
- 编解码代码里没有"若方向 == X 则特殊处理"的分支（grep `direction`/`is_from_js`，出现即人工复核）。

### 不通过示例

- UI→JS 用 `char *` JSON，JS→UI 用 tagged union → P0（无法保证往返等价）。
- `undefined` 在 UI→JS 转成 `null`，反方向转成 `0` → P0。

---

## 4. C3 Promise 与微任务泵送时机

### 要求

1. **只在 JS 栈深度为 0 时泵**。任何 native→JS 回调内部不得直接泵（不得重入）。
2. **泵必须可重入保护**：存在 `pump_depth`/`pumping` 标志，进入时断言，递归进入直接返回或延迟。
3. **在途请求有唯一终态**：每个请求 id 恰好 settle 一次（resolve 或 reject），重复 settle 是 P0。
4. **退订/取消要收尾**：取消后必须从 pending 表移除、释放对应 `JSValue`、且之后到达的响应被丢弃（不能 resolve 一个已取消的 promise）。
5. **pump 循环正确**：`JS_ExecutePendingJob` 返回值处理到 0 为止；每次非 0 都要检查异常并转成 rejected promise；异常不得穿出 C 边界。
6. **中断/超时**不能靠在 JS 执行线程里嵌套泵来实现。

### 检查方法

- 代码检查：

  ```bash
  grep -rn 'JS_ExecutePendingJob' src
  grep -rn -E 'pump|drain|microtask' src/bridge
  # 期望：泵只有一个入口；入口处有 depth/flag 断言
  ```

- 行为测试（必须存在）：

  | 用例 | 期望 |
  | --- | --- |
  | native 回调里同步 resolve 后再调 UI API（会触发泵） | 不递归；回调返回后才泵；顺序可预测 |
  | 同一请求 resolve 两次 | 第二次被拒绝并产生诊断，不改变终态 |
  | resolve 后立即 cancel | cancel 无效或返回"已完成"，不泄漏 |
  | cancel 后响应到达 | 丢弃；pending 表条目已删除；ASan 无泄漏 |
  | job 抛异常 | 对应 promise 变 rejected；进程不崩；异常对象被 `JS_FreeValue` |
  | 队列里 1000 个 job 连续泵 | 泵到空为止，期间不进入任何 native 回调重入 |

- 生命周期证据：运行 `ASAN_OPTIONS=detect_leaks=1`，上述用例全部零泄漏。

### 通过判据

- 泵入口唯一，且有可观测的 depth 断言（测试里能把断言触发出来）。
- 每个 pending 条目的创建/销毁点成对出现（review 时逐条对照）。
- 所有 Promise 都有且只有一个 settle 路径（代码里 settle 函数只有一个，且带状态机）。

### 不通过示例

- 每次 `JS_Call` 成功后立刻 `JS_ExecutePendingJob`，且该 `JS_Call` 可能来自 native 回调 → P0（重入）。
- 取消只从 pending 表移除但忘了 `JS_FreeValue` → P0（泄漏 + 悬垂）。
- 用 `errno` 传递 QuickJS 错误 → P0（审计 §3.3：`errno` 不可用）。

---

## 5. C4 线程安全

### 要求

1. **线程归属声明**：明确"QuickJS context 只属于线程 T"；所有公共 API 入口 `assert(current == T)`（release 构建转为返回错误码）。
2. **跨线程只走队列**：其它线程只能 enqueue 消息，不得触碰 `JSValue`、`JSContext`、Provider 对象。
3. **队列有界**：容量上限 + 满时策略（阻塞 / 拒绝 / 丢弃）必须显式，并有对应测试。
4. **`JSValue` 线程封闭**：队列元素里不得出现 `JSValue`（grep 检查结构体定义）。
5. **关闭顺序固定**：停止收新任务 → 排空队列 → 释放 context/runtime → join 线程；每步有状态标志。
6. **无数据竞争**：共享计数用 `_Atomic`，锁用 `pthread_mutex`；不使用"看起来原子"的普通 `int` 跨线程。

### 检查方法

```bash
# 1) 线程归属断言存在
grep -rn -E 'assert.*thread|thread_id|js_thread|owner_thread' src/bridge src/provider
# 期望：入口函数覆盖率 = 100%（逐函数对照）

# 2) JSValue 不得进入队列/跨线程结构
grep -rn -A15 'struct .*Queue\|struct .*Message\|struct .*Job' src | grep -E 'JSValue|JSContext'
# 期望：空

# 3) 数据竞争
cc -fsanitize=thread ...   # 或既有构建系统的 TSan 目标
# 期望：并发用例零 race 报告
```

- 测试：从非 JS 线程调用每个公共 API → 必须返回 `WRONG_THREAD` 或安全入队，绝不崩溃、绝不触碰 context。
- 测试：高并发入队 + 关闭，跑 1000 次，检查无死锁、无 UAF（ASan）。
- 测试：队列满时的策略与文档一致。

### 通过判据

- 入口断言覆盖率 100%（逐一列出 API 与断言位置）。
- 队列结构里无 QuickJS 类型。
- TSan + ASan 并发用例全绿。
- 关闭路径可重入、可重复调用。

### 不通过示例

- 在 UI 线程直接读 `JSValue` 判断类型 → P0。
- 用 `volatile int` 做跨线程标志 → P0。
- 队列无界 → P1（内存可被 JS 侧压爆）。

---

## 6. C5 生成代码"无魔法、可逐行审阅"

这一节把"无魔法"拆成**可机械判定**的 9 条规则。每条都要在 CI 里跑，不靠评审员印象。

### R1 生成物可字节重现

```bash
ct build --emit-c --ns-root . <unit>.h.ct -o out1
ct build --emit-c --ns-root . <unit>.h.ct -o out2
diff -r out1 out2
```

**通过**：差异为空（审计已验证 CT v0.19.1 满足）。任何时间戳/随机名/绝对路径进入产物即 P1。

### R2 每个导出符号可追溯到源文件行

```bash
ct compile <unit>.h.ct --emit-map - > map.json
```

**通过**：
- `.h` 里每个函数原型都能在 `map.functions` 或 `map.impls[].symbol` 命中，且 `file`/`line` 指向真实源码行；
- `map.traits` / `map.structs` 覆盖所有 `pub` 类型；
- 覆盖率脚本对"头文件声明集合 − map 集合"断言为空。差一个即 P1。

### R3 导出集合 = 声明集合

```bash
nm -gU build/libnexcore.dylib | awk '{print $3}' | sed 's/^_//' | sort > exported.txt
grep -oE '[A-Za-z_][A-Za-z0-9_]*\(' out/<unit>.h | tr -d '(' | sort -u > declared.txt
comm -3 exported.txt declared.txt
```

**通过**：只允许出现编译器模板符号（`__ct_init` 等）在白名单里；其余双向差集为空。多导出（内部函数意外可见）即 P1。

### R4 生成物只允许固定模板

**通过**：生成代码里允许出现的"非源行内容"仅限：

- 溯源 banner、include guard；
- `struct <Trait>_vtbl` / `struct <Trait>_iface` / `__<trait>_vptr` 字段；
- `_self` 强转行；
- `<S>_<T>_vtable` 及其 `__attribute__((unused))`；
- `__ct_init` 构造器（文件作用域对象/数组）。

出现其它编译器注入的控制流/宏/辅助函数 → 人工复核，无法解释即 P1。

### R5 业务逻辑不得藏在 raw / 宏里

```bash
grep -rn -E '#ct-raw|#define' src --include='*.ct'
```

**通过**：
- `#ct-raw` 只允许出现在 `src/provider/raw/`（或等价白名单目录），且每块上方必须有一行注释说明"为什么 CT 表达不了"；
- raw 块里不得包含协议分支、Promise 状态机、队列逻辑；
- 任何 `#define` 不得展开成控制流（`if/for/return`）。
- 违反 → P0（不可审阅）。

### R6 控制流显式

**通过**：
- 不出现 `setjmp`/`longjmp`/`goto` 跨函数（QuickJS 内部除外）；
- 不用宏做状态机跳转；
- 回调分派是一个显式 `switch` 或静态表，表项数与协议方法数相等（脚本比对）。

出现隐式控制流 → P1。

### R7 编译零警告

```bash
cc -std=c99 -Wall -Wextra -Werror -I out out/*.c host/*.c -o /tmp/x
```

**通过**：零警告。CT 生成物已对未用符号加 `__attribute__((unused))`；若仍告警，说明业务代码有问题。任何 warning 即 P1。

**必跑**：不能只看 `ct build --emit-c` 的退出码——它只打印 cc 命令、不执行。审计 §3.7 的 `&f` 静默错译就发生在这层：CT exit 0、零诊断，`cc` 才报 `use of undeclared identifier`。评审以真实 `cc` 结果为准，这类错误按 P0 处理。

### R8 行号映射可开关

```bash
ct build --emit-c --line  … -o out_line     # 带 #line，供定位
ct build --emit-c --no-line … -o out_plain  # 净版，供逐行阅读
```

**通过**：两份产物都在 CI 里生成；评审以净版逐行阅读，以带行号版定位问题。缺任一份即 P2。

### R9 单元职责与规模

**通过**：
- 每个 `.h.ct` 对应一个主类型/职责（文件头一行注释写明）；
- 单文件源码 ≤ 400 行、单函数 ≤ 60 行；超限必须在文件头写明拆分理由，否则 P2；
- 生成物行数 / 源码行数 的比值有上限（建议 ≤ 3.0，超出需解释模板膨胀来源）。

### 不通过示例

- 用 `#ct-raw` 写整个 Promise 状态机 → P0。
- 生成物里出现 `if (x) return;` 这类**不在源文件里**的分支 → P1（模板越界）。
- 头文件导出 30 个函数，`--emit-map` 只覆盖 20 个 → P1。

---

## 7. C6 生命周期与所有权（由审计 E 节直接推出）

### 要求

1. **trait 接口值 = 借用**。任何跨越 `return`、队列、线程、Host 边界的接口值，其 `self` 必须指向**堆对象**且有明确 owner。
2. Host 侧只持有**句柄**（`uint64_t` 或 `void *` + generation），不持有 CT 的 `_iface` 结构。
3. 对象销毁后，句柄必须变成 tombstone（generation 不匹配即返回 `OBJECT_GONE`），不得解引用已释放内存。
4. 释放顺序：Host 释放 wrapper → Provider 释放对象 → 清 pending → 释放 JSValue。

### 检查方法

```bash
# 返回 trait 接口的函数：逐个复核（ct check 不会报）
grep -rn -E '^\s*[A-Z][A-Za-z0-9_]*\s+[a-z_]+\(.*\)\s*\{' src
# 人工/脚本判定返回类型是否是 trait 名

# 生命周期用例（必须存在）
ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1 ./lifecycle_tests
```

必备用例：
- 对象销毁后再用句柄调用 100 次 → 全部返回 `OBJECT_GONE`，零崩溃；
- Host 持有 wrapper 时 JS 侧 GC/释放对象 → 不悬垂；
- 队列里还有针对该对象的请求时销毁 → 请求被拒绝并清理。

### 通过判据

- 不存在"返回局部变量装箱的接口值"（审计已证明这是零诊断的 ASan 错误）。
- 所有句柄访问都经过 generation 校验。
- ASan/LeakSanitizer 全绿。

### 不通过示例

```c
Node make(void) { Num n = { 3 }; return n; }   /* P0：返回栈上对象的胖指针 */
```

---

## 8. C7 错误、取消与边界

### 要求

1. 每个 FFI 入口返回显式状态码；禁止用 `errno`（审计 §3.3）。
2. C 库错误经 `.shim.h` 转成返回值或 out 参数；shim 里才允许出现 `errno`。
3. JS 异常必须在 Provider 内消费：`JS_GetException` → 转成错误码/值 → `JS_FreeValue`；异常对象不得穿出 C 边界。
4. 错误分类表唯一（与协议类型表同一份或明确引用）。
5. 取消与超时是终态，必须与"失败"区分。

### 检查方法

```bash
grep -rn '\berrno\b' src --include='*.ct'     # 期望：空（只允许出现在 .shim.h/.c）
grep -rn 'JS_GetException' src/provider        # 期望：每个 catch 点成对出现 JS_FreeValue
```

- 测试：让 Provider 每个方法都抛异常，断言 Host 收到 `JS_ERROR` 且进程不崩、无泄漏。
- 测试：非法句柄、已取消请求、超时三者返回不同错误码。

### 通过判据

- `errno` 在 `.ct` 中出现次数 = 0。
- 异常路径有 ASan 证据。
- 错误码表与协议文档一致。

---

## 9. 评审流程与证据包

### 9.1 每次评审必须附的证据包

| # | 证据 | 来源 |
| --- | --- | --- |
| 1 | 构建日志（`ct build` 全量） | CI |
| 2 | 生成的 `.h` + `--emit-map` JSON | CI artifact |
| 3 | `nm -gU` 导出符号清单 | `build.sh` |
| 4 | `cc -Wall -Wextra -Werror` 结果 | CI |
| 5 | 两次构建 `diff -r` 结果 | CI |
| 6 | ASan / TSan 运行日志 | CI |
| 7 | 协议往返测试报告（类型表覆盖数） | 单测 |
| 8 | 越界引用检查（§2 三条 grep）输出 | CI |

缺 1、2、3、5、6 任一项 → 评审直接退回，不进入内容审查。

### 9.2 抽样规则

- **ABI 边界 100% 审**：每个导出函数、每个 struct 布局、每个回调签名。
- **生成代码 100% 对照一次**：净版产物与源文件逐行对照（产物规模小，成本可控）。
- **协议处理随机抽 30%**，但错误路径与取消路径 100% 审。
- **并发路径 100% 审**（队列、锁、关闭、线程归属）。

### 9.3 Finding 格式

```text
[P0] src/bridge/pump.ct:88 — 在 native 回调内直接调用 JS_ExecutePendingJob，存在重入
理由：违反 C3-1；回调栈深度 > 0 时泵会把嵌套 job 插到当前调用中
建议：泵只在 depth == 0 的调度循环里调用；回调内改为 enqueue
证据：pump_depth 未断言；测试 callback_nested_pump 复现
```

### 9.4 结论模板

```text
结论：不通过 / 有条件通过 / 通过
P0：n 条（列 id）
P1：n 条（列 id）
P2：n 条（进待办）
未验证项：（逐条列出，不允许留空当成通过）
```

---

## 10. 快速自检清单（提交前作者自查）

- [ ] Provider 之外 `grep JS_` 为空；公共头不含 `quickjs.h`
- [ ] 所有跨语言 struct 是纯字段 + `_Static_assert(sizeof)`；无 `use Trait` 的 struct 出现在 ABI
- [ ] 全部 ABI 整数是定宽类型（`int32_t`/`int64_t`），无裸 `int`/`long`
- [ ] 回调注册有 wrapper；`ns::fn` 未出现在值位置
- [ ] 每个公共入口有线程归属断言；队列有界；队列结构无 `JSValue`
- [ ] 泵入口唯一、有 depth 断言；pending 表创建/销毁成对
- [ ] 取消后不再 resolve；重复 settle 被拒
- [ ] `.ct` 中 `errno` 出现 0 次；无文件内 `#ifdef`；`import` 全在 `namespace` 之前
- [ ] 无"返回局部变量装箱接口值"
- [ ] 两次构建 `diff` 为空；`--emit-map` 100% 覆盖导出符号
- [ ] 无导入函数的裸名取址/裸调用；`cc -Werror` 真跑过，不是只看 `--emit-c` 退出码
- [ ] `-Wall -Wextra -Werror` 零警告；ASan/TSan 绿

---

## 11. 与审计事实的对应关系（为什么这些判据成立）

| 判据 | 依赖的语言事实（审计章节） |
| --- | --- |
| C1 隔离 | 生成物是普通 C，没有任何访问控制；分层只能靠构建/符号边界（§A、§H） |
| C2 对称 | trait/iface 是裸结构体，没有运行时类型信息；对称性只能靠同一编解码器保证（§E） |
| C3 泵 | 无语言级异步/调度；泵完全由 C 代码调度（§F、§G） |
| C4 线程 | 语言与 std 无线程原语；JS 单线程假设无人保证（§F） |
| C5 可审阅 | 产物可重现、`#line`、`--emit-map` 三项能力已实测（§H） |
| C6 生命周期 | 接口装箱是借用指针且有 ASan 复现的悬垂（§E） |
| C7 错误 | `errno` 不可用；异常必须手工消费（§3.3、§G） |
