# experiments/ — CT ↔ QuickJS 地基验证

> 结论先行：**CT 写的桥接层可以直接嵌入 QuickJS，双向调用、Promise 异步、三类错误路径全部实测跑通。**
> 本目录只放验证代码与实测记录，不是正式骨架。CT v0.19.1 / QuickJS 2025-09-13 / macOS arm64。

复现：

```bash
./experiments/build.sh          # 缺 QuickJS 时会自动下载并编译 libquickjs.a
```

产物全部在 `experiments/out/`（已 gitignore）；QuickJS 源码树在 `experiments/third_party/`
（同样 gitignore，不进仓库）。

---

## 1. 最小链路（`src/bridge.ct` + `js/bridge_test.js`）

CT 侧创建 runtime/context、注册 6 个 native 函数、执行 JS 文件，然后在自己的主循环里
drain 异步完成项并泵微任务队列。真实输出：

```text
[js] sync nativeAdd(2,3)=5
[js] sync nativeAdd(-4,10)=6
[js] native error caught: native refused the call
[js] script tail reached (async still pending)
[c] called JS jsMultiply(6) -> 42
[c] async scheduled arg=21 slot=0
[c] resolved slot=0 arg=21 result=42
[js] async resolved=42
[c] async-reject scheduled arg=5 slot=1
[c] rejected slot=1 arg=5
[js] async reject caught: worker rejected the job
[c] pump loop spins=118 jobs=2 microtasks=2 done=1
[c] JS throw -> JS_IsException=1
[c] exception from throw: Error: js boom
[c] clean shutdown
```

覆盖的链路：

| 链路 | 实测结果 |
| --- | --- |
| JS → native → JS（同步） | `nativeAdd(2,3)=5`，`JSValue` 按值进出无碍 |
| CT → JS 函数（反向） | `JS_GetPropertyStr` + `JS_Call`，`jsMultiply(6)=42` |
| JS 抛异常 → CT | `JS_Eval` 返回 `JS_TAG_EXCEPTION`，`JS_GetException` 取到 `Error: js boom` |
| native 返回错误 → JS | `JS_ThrowTypeError`，JS 侧 `try/catch` 收到 `native refused the call` |
| Promise resolve | pthread worker 完成后由主线程 resolve，`await` 后续代码继续执行 |
| Promise reject | 主线程调用 reject 函数，JS 侧 `catch` 收到字符串 reason |

### 微任务泵的时机

```text
主循环每次迭代：
  drain(b)                # 主线程检查 Job.done，调用 resolve/reject
  JS_ExecutePendingJob()  # 泵微任务，直到返回 0
  usleep(1ms)             # 实验用轮询；真实实现应由宿主事件循环驱动
```

- worker **只写** `Job`（mutex 保护），绝不碰 `JSValue` / `JSContext`。
- `JS_Call(resolve)` 之后 Promise 的状态变更会排入 runtime 的 job 队列，
  下一次 `JS_ExecutePendingJob` 才真正执行 `await` 之后的续体。
- 实验里 2 个 Promise 共泵出 2 个微任务，`done=1` 后退出。
- 注意：`JS_SetContextOpaque` 传的是一个 `Bridge *`；CT 没有闭包，回调上下文只能这样显式传。

---

## 2. C ABI 导出（`src/abi.h.ct`）

```bash
ct build --emit-c experiments/src/abi.h.ct -o <dir>   # 生成 abi.c / abi.h
```

**重要坑：`ct compile x.h.ct` 生成的 `.h` 只有一个 include guard（“Phase 0: types only”）；
`pub` 函数/结构体/typedef 的原型只有在 `ct build --emit-c` 的多文件模式下才进 `.h`。**

导出的 `abi.h` 实测包含：

```c
struct NexcorePair;
typedef int (*NexcoreCallback)(void *, int);
struct NexcorePair {
    int32_t left;
    double right;
    void * ctx;
};
int32_t nexcore_abi_add(int32_t, int32_t);
...
struct NexcorePair nexcore_pair_make(int32_t, double, void *);
int nexcore_apply(void *, NexcoreCallback, int);
```

消费方实测：

```text
C    : host add=42 version=1 / pair sizeof=24 right_off=8 ctx_off=16
       host make left=7 right=2.5 ctx_ok=1 / host apply=42
Swift: swift add=42 version=1 / pair size=24 right_off=8
       swift make left=7 right=2.5 ctx_ok=true / swift apply=42
```

- Swift 用 `swiftc -import-objc-header abi.h host.swift abi.o` 直接消费；
  `struct` 按值返回、函数指针回调 + `void *userdata` 都正常。
- Kotlin：本机只有 JVM 版 `kotlinc`，没有 `kotlinc-native`，因此未实测 cinterop；
  Kotlin/Native 消费的是同一份 `abi.h`，JVM 则需要 JNI 包一层。这是工具链差异，不是 CT 的限制。
- `nm -gU` 可看到 `_nexcore_abi_add` 等全局符号。

### 函数指针 / 上下文

- CT 支持 `ret (*name)(params)`、`typedef` 函数指针、struct 字段、数组（language-spec §4.12）。
- **没有闭包**。回调需要上下文时只能走 `void *userdata`（本实验的 `NexcoreCallback`）
  或 `JS_SetContextOpaque`（QuickJS 单 context 场景）。
- `JS_NewCFunction` 要求精确签名；CT 里写
  `JSValue f(JSContext *, JSValueConst, int, JSValueConst *)` 可零警告通过 C 编译器校验。

---

## 3. 结构体布局（`src/layout.ct` vs `host/layout_ref.c`）

CT 与手写 C 的 `sizeof` / `offsetof` 逐行 `diff` 完全一致：

```text
sizeof_char=1  sizeof_int=4  sizeof_long=8  sizeof_size_t=8
sizeof_voidp=8 sizeof_double=8 sizeof_longdouble=8 sizeof_bool=1
sizeof_enum=4  align_double=8
layout_size=40 off_c=0 off_i=4 off_l=8 off_d=16 off_p=24 off_b=32 off_e=36
```

结论与风险：

- CT 不重排布局，生成的就是普通 C 结构体，与 C/Swift 的布局一致。
- **ABI 等价类型会静默通过**：CT 不做类型检查，`long` / `int64_t` / `size_t` 写错也照样编译。
  实测 macOS arm64 上 `sizeof(long double)=8`（x86 Linux 通常是 16），`long` 在 Windows LLP64 是 4。
  → 跨语言 ABI 结构体只用定宽类型（`int32_t` / `int64_t` / `double` / `void *`），
  不要把 `long` / `long double` / 裸 `enum` 放进共享布局。

---

## 4. 线程与跨线程任务队列

- CT **没有语言级线程原语**；但 `#include <pthread.h>` 直通后，
  `pthread_create` + `static void *worker(void *)` 函数指针回调实测可用（`src/bridge.ct`）。
- 推荐方案（已验证的形状）：
  1. QuickJS runtime/context 只属于一个 JS 线程（宿主主线程）。
  2. worker 线程只产出「纯数据结果 + job id」，用 mutex/condvar 入队。
  3. JS 线程 drain 队列 → 调用 `resolve` / `reject` → `JS_ExecutePendingJob`。
  4. 泵的驱动交给宿主事件循环（macOS `CFRunLoop` / Android `Looper`）或专用 JS 线程 + wakeup fd；
     实验里的 `usleep(1ms)` 只是轮询占位。
- 不要从 worker 直接调用任何 `JS_*` API；本实验全程遵守这一点。

---

## 5. 已知语言缺口的实测结果

| 缺口 | 实测行为 | 绕法 |
| --- | --- | --- |
| `ct compile` 下的 `import` | `error: 'import' is not supported in compile mode`（与 `.ct` / `.h.ct` 无关） | 多单元一律走 `ct build` |
| 普通 `.ct` 能否 import | **可以**（`ct build` 模式实测通过；被 import 的单元必须是 `.h.ct` / `.m.ct`） | 无 |
| `import` 写在 `namespace` 之后 | 不报错，生成的 `.c` 里**没有** `#include "abi.h"`，调用变成无原型调用（链接期才炸） | import 永远放文件最前 |
| `errno` 标识符 | `error: use of an undeclared variable` | `static inline int shim_get_errno(void){return errno;}` 放进 `.shim.h` |
| 文件内 `#ifdef` | `error: expected an expression` | 条件编译移进被 `#include` 的头文件 |
| `ns::fn(...)` 解析歧义 | **未复现**：同命名空间 `demo::helper(1)` 与跨命名空间 `other::twice(21)` 均正确解析为 `demo_helper(1)` / `other_twice(21)` | 若后续遇到，优先用导入后裸名或 `#ct-raw` shim |
| `ct build --emit-c` 输出目录不存在 | `ctc: cannot write the output file`（提示不明确） | 先 `mkdir -p` 目标目录 |

补充：带 `namespace` 的单元在 `ct build` 下要求「目录名 == namespace」，
并用 `--ns-root <dir>` 指定根，否则报
`ctc: namespace 'demo' does not match the directory ''`。

---

## 6. 对外部 C 库的实际链接方式

```bash
# 一次性构建依赖
make -C experiments/third_party/quickjs libquickjs.a

# 编译 + 链接
ct compile experiments/src/bridge.ct -o experiments/out/bridge.c
cc -I experiments/third_party/quickjs \
   experiments/out/bridge.c experiments/third_party/quickjs/libquickjs.a \
   -lm -lpthread -o experiments/out/bridge
```

- 头文件通过 `.ct` 顶层的直通 `#include "quickjs.h"` 可见；CT 不需要为 `JSRuntime` /
  `JSContext` / `JSValue` 写声明，未知类型名原样发射给 C 编译器。
- `JSValue`（16 字节结构体）按值作为参数和返回值实测可用。
- 静态库直接列在 `cc` 行；换成 `.dylib` / `.so` 也只是 `-L -l`。

---

## 文件地图

```
experiments/
├── build.sh                    全部验证的一键入口
├── src/
│   ├── bridge.ct               同步 + 反向调用 + 异步 Promise + 错误路径
│   ├── abi.h.ct                pub C ABI（函数 / struct / typedef / 函数指针）
│   ├── layout.ct               与 C 对照的 sizeof/offsetof
│   ├── crossunit_demo.h.ct     .h.ct 作 importer
│   └── crossunit_plain.ct      普通 .ct 作 importer（build 模式）
├── js/bridge_test.js           异步/错误路径脚本
├── host/                       C 与 Swift 消费方、C 布局参照
├── gaps/                       语言缺口探针与绕法
└── out/                        生成物与实测日志
```
