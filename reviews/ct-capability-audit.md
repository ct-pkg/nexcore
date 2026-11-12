# CT 语言能力边界独立核查

| 项 | 值 |
| --- | --- |
| 核查对象 | `ct` 编译器及其生成物（桥接层要实现的部分） |
| CT 基线 | `ct v0.19.1`，仓库 `c27da3d`（只读） |
| 核查方式 | 只读 CT 文档/源码 + 在 `/tmp/ctreview` 独立跑最小实验（不读编码角色产出） |
| 核查日期 | 2026-11-06 |
| 判定用语 | **可用** / **受限**（有条件可用） / **不可用** / **未验证**（没有实证，仅推断） |

> 实验目录 `/tmp/ctreview` 是临时目录，未写入本项目任何目录；本文只落 `reviews/`。

---

## 1. 结论速览

| # | 能力 | 判定 | 一句话结论 |
| --- | --- | --- | --- |
| A | 导出 C ABI | **可用（有命名约束）** | 根级 `pub` 函数即裸 C 名；命名空间内 `pub` 发射为 `<ns>_<name>`。`.h` 只在 `ct build` 模式生成完整声明，单文件 `ct compile` 的 `.h` 是空壳 |
| B | 函数指针 / 回调 | **可用（值位置受限）** | `typedef` 函数指针、字段、参数、数组都支持；可携带 `void *ctx` 上下文。命名空间限定的函数名只能出现在调用位置；作值时限定名被拒，**导入函数的裸名在根单元会被静默错译（§3.7）** |
| C | 结构体布局 | **受限（可用但不是"透明结构体"）** | 字段顺序/拼写原样保留；一旦 `use Trait`，**尾部追加隐藏 vptr**，布局与纯 C 镜像不同。编译器不对布局做任何校验 |
| D | ABI 等价类型 | **不安全（静默接受）** | `int→long`、`char*→void*`、`double→float`、`unsigned→int` 全部静默通过 `ct check`；只有 struct 值/struct 指针的**类型名**会被比对 |
| E | trait 语义 | **可用，但纯借用语义** | trait 作类型 = 16 字节胖指针 `{self, vtbl}`；装箱取**原对象地址，不拷贝**；绑到局部变量并逃逸即悬垂（ASan 已复现） |
| F | 线程 / 并发原语 | **语言与标准库都没有** | `std/` 无任何 thread/mutex/atomic；`pthread`、`_Atomic`、`__thread` 靠 C 透传可用（已编译运行） |
| G | QuickJS 互操作 | **可用（形状级验证，未链接真实库）** | 不透明句柄、`JSValue` 按值返回/传参、union 成员、函数类型 `typedef`、宏调用、变参全部走通 |
| H | 生成物可审阅性 | **可用** | 产物逐字节可重现；`#line` 可开可关；`--emit-map` 给出 trait/struct/函数/impl → C 符号的源文件行号 |
| I | 导入函数的裸引用 | **P0 静默错译** | 根单元 `&f` → 原样发射 `&f`（应为 `a_b_mod_f`）；命名空间单元裸调用 `f(1)` → 原样发射。CT exit 0、零诊断，`cc` 才报 undeclared（§3.7） |

---

## 2. 逐项核查

### A. C ABI 导出

**导出形态（实测）**

根命名空间（无 `namespace`）的 `pub` 函数保持裸名：

```c
/* abi/nxbridge.h.ct */
pub int nxbridge_ping(int x) { return x + 1; }
```

```c
/* 生成的 nxbridge.h */
int nxbridge_ping(int);
```

命名空间内的 `pub` 会被 mangling：

```c
/* namespace nexbridge; pub int nxb_add(...) → 生成的 core.h */
int nexbridge_nxb_add(int, int);
struct nexbridge_Stats { int calls; int last; };
typedef void (*nexbridge_evt_cb)(void *, int);
extern int nexbridge_g_hits;
```

- mangling 规则：命名空间各段用 `_` 连接 + `_` + 名字（`namespace deep::inner` → `deep_inner_*`）。
- `extern` 声明**不参与 mangling**（它指向外部 C 符号）。
- 导出的 `pub struct` 若 `use` 了 `pub trait`，头文件里同时出现 `struct <T>_vtbl`、`struct <T>_iface`、`extern <S>_<T>_vtable`，且 struct 定义**带隐藏 vptr 字段**：

```c
struct abi2_Obj {
    int n;
    struct abi2_Counter_vtbl *__abi2_counter_vptr;
};
```

**Swift / Kotlin 侧看到什么**

- Swift：生成的头是普通 C99 头，可直接进 module map（`#include` 即可），符号带 `_` 前缀由链接器补。可 `dlopen`/静态链接。
- Kotlin：C 头不能直接调用，必须再包一层 **JNI**（`Java_*` 导出名）；CT 这一侧只负责提供稳定的 C 函数与结构体。
- 打包：`ct build --cflags '-fPIC -shared' --ldflags '-shared' -o libx.dylib …` 可产出 dylib；`nm -gU` 能看到导出的 `_bridge_*` 符号。

**重要坑：头文件生成模式不一致**

| 命令 | `.h` 内容 |
| --- | --- |
| `ct compile x.h.ct -o x.c`（单文件） | 只有 include guard + banner（"Phase 0: types only"），**没有原型** |
| `ct build --emit-c --ns-root . <dir>/x.h.ct`（多文件） | 完整 `pub` 类型、typedef、函数原型、`extern` vtable |

桥接层的构建脚本必须走 build 模式，否则 Swift/JNI 侧拿不到声明。

---

### B. 函数指针与回调

支持（均实测通过）：

- `typedef int (*nx_cb)(void *ctx, int code);`
- 函数指针作参数、struct 字段、全局初始化器、数组元素；
- `p = handler;` / `p = &handler;` / `(*p)(args)` / `p(args)`；
- 变参函数指针（QuickJS 的 `JS_ThrowTypeError` 形状可用）。

上下文指针就是普通 `void *`。C 调 CT 的往返实测：

```c
/* CT 导出 */ int bridge_fire(bridge_evt_cb cb, void *ctx, int code);
/* C 侧 */    bridge_fire(on_evt, &got, 42);   /* 输出 callback code=42 / got=42 */
```

**限制（会直接影响桥接注册表）**

1. **限定函数名只能出现在调用位置**。以下两种写法都被拒：

   ```c
   int (*p)(int) = nsa::f;     /* error: no variable of that name in that namespace
                                  (qualified type names are not supported; use the bare name or an alias import) */
   run(nsa::f, 2);             /* 同上 */
   ```

   裸函数名（同单元 `static`/根级）作值正常。桥接层若要把 Provider 的命名空间函数注册成回调，必须写一层 wrapper，或把回调函数定义在根命名空间/同单元。
2. 不支持嵌套函数指针（`int (**f)(int)`）、函数指针数组作参数、返回函数指针。

---

### C. 结构体布局稳定性

**稳定的一半**：字段体原样保留（顺序、拼写、`__attribute__` 原样带出），由 C 编译器决定 padding/对齐；`_Static_assert(sizeof(...) == N, ...)` 可用，适合做布局回归门禁。

**不稳定的一半**：

- `struct S { … } use T` 会在**末尾**追加 `struct T_vtbl *__t_vptr;`。实测（ct v0.19.1 @ c27da3d，macOS arm64）：
  - `Person { char *name; int age; }` 不带 trait：`sizeof=16`，`name@0`、`age@8`；
  - 加 `use Greeter`：`sizeof=24`，`name@0`、`age@8`、`__greeter_vptr@16`。
- 多个 trait 按 `use` 顺序**各追加一个** vptr：`Pair { int x; char y; } use First, Second` → `sizeof=24`，`x@0`、`y@4`、`__first_vptr@8`、`__second_vptr@16`，即 **2 个 vptr**。
- 以上两个结构体 `ct check` **exit 0、零诊断**。但 CT 源码里引用隐藏成员会报错：`error: struct 'Person' has no field '__greeter_vptr'` —— 说明该成员只存在于生成的 C 里，不属于 CT 层类型。
- 编译器**不校验**镜像布局：`bindings/README.md` §4 明写"镜像布局就是 ABI 契约，不一致时行为未定义（v1 不做校验）"。
- 注意语法：`trait` 定义末尾**不能**写 `};`，会报 `expected a type`。

**对跨语言的含义**

- 任何跨语言传递的 struct 只能有两种安全形态：① 纯字段 struct（不带 `use Trait`），并用 `_Static_assert` 钉住 `sizeof`/关键 `offsetof`；② 完全由 CT 侧分配、对侧只拿不透明指针。
- 不要让 Swift 端按"字段列表"自己镜像一个带 trait 的 struct——隐藏 vptr 会让 `sizeof` 和字段偏移全错。
- 导出头里出现 `__<trait>_vptr` 是**正确信号**，不是生成器 bug。

---

### D. ABI 等价类型会被静默接受

`ct check` 的实参类型检查只覆盖 struct 名字。下面全部**无诊断通过**：

```c
long  f_long(long);
void  g_ptr(char *);
void  g_void(void *);

f_long(i);      /* int → long   静默 */
g_ptr(vp);      /* void* → char* 静默 */
g_void(cp);     /* char* → void* 静默 */
f_short(i); f_char(i);            /* 静默 */
f_double(fl); f_float(d);         /* 静默 */
f_uint(i); f_sizet(i);            /* 静默 */
```

唯一会被抓的是：

```
error: type mismatch: 'f_aval' argument 1: expected 'struct A', got 'struct B'
error: type mismatch: 'h_a' argument 1: expected 'struct A *', got 'struct B *'
```

**后果**：如果桥接层手写头文件（Swift/JNI 侧）与 CT 侧签名不一致，`ct check` 不会兜底；能兜底的只有 C 编译器的原型转换或运行期崩溃。因此：

- 跨语言边界一律用 **定宽类型**（`int32_t` / `int64_t` / `uint32_t` / `size_t`），不要用 `int`、`long`、`unsigned long`。
- CI 必须把生成的头文件当作唯一事实来源，禁止第二份手写原型。
- 注意 `int` 在 LP64 下是 32 位，而 Swift `Int` 是 64 位、Kotlin `Int` 是 32 位 —— 两侧的"默认整数"根本不是同一个东西。

---

### E. trait 的实际语义

**结论：装箱转发（按地址），不是值拷贝。**

```c
struct Greeter_iface { void *self; struct Greeter_vtbl *vt; };   /* 16 字节 */
```

| 源码 | 生成 C | 语义 |
| --- | --- | --- |
| `Person p; Greeter g = p;` | `{ (void *)&p, &Person_Greeter_vtable }` | 取 `p` 的地址，**不复制** |
| `Greeter h = g;` | 复制 16 字节胖指针 | `self` 仍指向原对象 |
| `void f(Greeter w)` | `void f(struct Greeter_iface w)` | 按值传胖指针，仍是借用 |
| `p.greet()` | `p.__greeter_vptr->greet(&p)` | 对象内嵌 vptr |

**生命周期后果（ASan 实证）**

```c
Greeter make_local(void) {
    Person p = { "local", 7 };
    return p;              /* 返回指向栈帧的胖指针 */
}
```

编译零警告、`ct check` 零诊断；运行到 `make_local().greet()` 时：

```
AddressSanitizer: stack-use-after-return
#0 Person_Greeter_greet traitdemo.c:31
Address … is located in stack of thread T0 … in frame make_local … 'p'
```

**对桥接层的含义**

- trait 接口值 = **借用指针**，不是所有权对象。任何跨 `return`、跨队列、跨线程保存的接口值，都必须自行保证被指向对象活得够久（堆分配 + 显式 owner）。
- 不要指望 `ct check` 发现悬垂：它不会。这必须靠代码规约 + ASan + 生命周期测试兜底。
- vtable 在"`pub` trait × `pub` struct"之外都是 `static`，跨单元拿不到；Provider 的对象如果要被上层使用，导出面应设计成**标量/不透明指针入口**，把装箱留在同一单元内（`bindings/mock-hal` 就是这个形态）。
- 多 trait 同名方法：裸调用按 `use` 顺序取第一个，且没有冲突诊断（CT 台账 SH-46）。桥接层不应依赖"同名方法"这种设计。

---

### F. 线程与并发

**语言层与标准库都没有原语。** `std/` 下只有 `arena / fs / map / str / test / utf8 / vec`，全局搜索无 `pthread` / `mutex` / `atomic` 命中。

但 C 透传可用（实测编译并运行通过）：

```c
#include <pthread.h>
#include <stdatomic.h>

static atomic_int g_count = 0;
static _Atomic long g_total = 0;
static __thread int g_tls = 0;      /* 或 _Thread_local */
```

- `pthread_create/join`、`pthread_mutex_t`、`atomic_fetch_add`、`atomic_load`、TLS 均正常。
- `#ct-raw` 也能注入上述声明，但**raw 块里声明的变量对 CT 符号表不可见**：CT 代码里 `g_tls = x;` 会报 `use of an undeclared variable`；调用 raw 里的函数则正常。所以线程相关状态必须藏在 raw/shim 的函数后面，不能裸用变量名。

**"跨线程任务队列"这条设计要求桥接层自己实现什么**

1. 线程创建与生命周期（pthread + 启动/停止握手）；
2. 互斥量 + 条件变量，队列的入队/出队/唤醒；
3. 有界队列与背压策略（拒绝/阻塞/丢弃必须显式定义）；
4. JS 线程归属断言（语言不会帮你保证"JS 只在一条线程跑"）；
5. `JSValue` 的线程封闭：跨线程只传不透明句柄/ID，绝不传 `JSValue`；
6. 关闭顺序：停止收任务 → drain → 释放 QuickJS 上下文 → join 线程。

以上全部只能用 C（`.shim.h/.c` 或 `#ct-raw` + 函数封装）写，CT 不提供语法级支持。

---

### G. 与 QuickJS 互操作

**验证方式**：按评审独立性要求，本次不读取 `experiments/` 下编码角色的验证产出（其中已有一份 QuickJS 副本）。改为按 QuickJS 公开 API 的**类型形状**自写一组 stub（`qjs/quickjs_stub.h` + `stub.c`），CT 代码照真实用法编写，编译、链接、运行通过。

覆盖到的形状：

| QuickJS 形状 | 结果 |
| --- | --- |
| `typedef struct JSContext JSContext;`（不透明） | 可用 |
| `JSValue` 16 字节、含匿名/具名 `union u`、按值返回与传参 | 可用（`r.u.int32` 正常） |
| `typedef JSValue JSCFunction(JSContext *, JSValueConst, int, JSValueConst *);`（函数类型而非指针） | 可用 |
| `JS_NewCFunction(ctx, fn, name, len)` 宏 | 可用（CT 按调用透传，C 预处理器展开） |
| `JS_SetContextOpaque` / `JS_GetContextOpaque` 携带桥接对象 | 可用 |
| `JS_ThrowTypeError(ctx, fmt, ...)` 变参 | 可用 |
| `JS_Call` 返回 `JSValue`（struct by value） | 可用 |

实测输出：`result=42 calls=1`。

**限制与注意**

- CT 不理解引用计数：每个 `JSValue` 的 `JS_FreeValue` 必须显式调用；CT 的 `own` 只覆盖 `struct * + malloc + use Drop`，**不适用**于 `JSValue`。
- `JSValue` 的 NaN-boxing 是 union 字段级访问，CT 不做任何标签检查；`JS_GetException` 的返回值必须被消费并释放，否则泄漏。
- `JS_NewCFunctionData` 的 `func_data` 数组可传 `JSValue *` 指针（指针参数不受函数指针数组限制）。
- 真实 QuickJS 头里有大量 `#ifdef` / 平台宏 —— 这没问题，因为 **CT 不解析被 `#include` 的头**，只透传。但**你自己**的 `.ct` 里不能写 `#ifdef`（见 §3.4）。
- **未验证**：与真实 `libquickjs.a` 的链接、`JS_ExecutePendingJob` 循环、GC 交互、`JS_SetInterruptHandler` 的实际行为。

---

## 3. 已知缺口核实

> 每项给出：触发条件、最小复现、实际报错/后果、绕法。

### 3.1 导入方向与模式：谁能 import、谁能被 import

- **作为被导入方**：普通 `.ct` 不行。
  - 命令：`ct build --emit-c -I . --ns-root . imp_plain.ct -o out/imp_plain`（`import helper;`，磁盘上只有 `helper.ct`）
  - 输出：`ctc: only a .h.ct unit can be imported: rename 'helper.ct' to 'helper.h.ct'`（exit 1）
  - `.h.ct` 与 `.m.ct` **都可以**被 import。`.m.ct` 实测：`import nsm::sys;` 后调用 `nsm::Put.put(65)`，生成 `return cput((65));`，不产出独立 `.c`。
- **作为导入方**：
  - `ct build` 模式：普通 `.ct` 可以 import `.h.ct` / `.m.ct`（实测 exit 0，产物含被导入单元的 `.c`）；
  - `ct compile` 模式：文件里出现任何 `import` 都直接拒绝：
    `imp_plain.ct:1:1: error: 'import' is not supported in compile mode`（exit 1）。
- **结论**：原表述"普通 `.ct` 不能被 import"指的是**被导入方**；作为**导入方**它在 `ct build` 下是正常的。审计初稿漏写了 `ct compile` 的限制，此处更正。
- **绕法**：对外暴露接口的单元一律命名 `.h.ct`；单文件检查用 `ct compile` 时不要带 import。

### 3.2 `import` 写在 `namespace` 之后会被静默丢弃

- **触发**：文件第一行 `namespace late;`，第二行 `import bridge::core;`。
- **实际**：编译 exit 0，生成的 `.c` **既没有 `#include "core.h"` 也没有被导入单元的 `.c`**；对 `bridge_set_hits` / `bridge_get_hits` 的调用原样发射，无原型。CT 阶段零诊断，直到 C 编译阶段才炸：

  ```
  error: call to undeclared function 'bridge_get_hits'; ISO C99 and later do not support
         implicit function declarations
  ```

- **根因**：编译器单趟顺序扫描，`namespace` 之后的行按普通 C 透传处理，`import` 只在文件头被识别。
- **绕法**：`import` 必须全部写在 `namespace` 之前；CI 加一条"import 必须在首行/必须早于 namespace"的 lint。

### 3.3 不支持 `errno` 标识符

- **触发**：CT 代码里读/写 `errno`（即使 `#include <errno.h>`）。
- **实际**：`errno_test.ct:7:11: error: use of an undeclared variable`（exit 1）。
- **根因**：CT 不展开 `#include`，`errno` 这类宏/全局量不在符号表；函数调用（`printf`/`fopen`）不受影响，因为调用目标可以未知。
- **绕法**：放 `.shim.h` 里做 `static inline int ct_errno(void) { return errno; }`（`bindings/posix-socket` 就是这么做的），或经 `.m.ct` 映射。

### 3.4 不支持文件内 `#ifdef`

- **触发**：`.ct` 文件里写 `#ifdef USE_A / #else / #endif` 包住声明。
- **实际**：CT 把 `#ifdef`/`#else`/`#endif` 行丢掉，但**两个分支的内容都照常解析并发射**：

  ```c
  static int pick(void) { return 11; }
  static int pick(void) { return 22; }
  ```

  C 编译器报 `error: redefinition of 'pick'`。
- **绕法**：条件编译只能放进 `.shim.h` / 外部头（CT 不解析），或拆成不同构建目标。CT 代码里不要出现条件分支声明。

### 3.5 限定名与裸名的解析规则

规则如下（ct v0.19.1 @ c27da3d 实测）。**调用位置与取值位置走的不是同一条解析路径，命名空间处理也不一致：**

- **调用位置（名字后紧跟 `(`）**：根单元写绝对路径 `a::b::mod::f(1)` 正确；命名空间单元写任何前缀（`mod::f` / `b::mod::f` / `a::b::mod::f`）都报 `no function of that name in that namespace`——前缀是按当前命名空间**相对**解析的，重复当前前缀会双重拼接。
- **取值/引用位置（名字后不跟 `(`）**：根单元 `&f`（导入函数、裸名）被**原样发射成 `&f`**，CT 退出 0 且零诊断（P0，见 §3.7）；`&a::b::mod::f` 报 `no variable of that name in that namespace (qualified type names are not supported; use the bare name or an alias import)`；命名空间单元里 `&f` 报 `use of an undeclared variable`。
- **类型位置**：`struct ns::Type` 报 `expected a variable name`；`ns::Type` 报 `no variable of that name…`；裸名 `struct Type` 或 `import …::Type as Type` 可用。

| 写法 | 结果 | 位置/退出码 |
| --- | --- | --- |
| `int x = nsa::f(1);` | 通过 | exit 0 |
| `nsa::f(1);`（独立语句） | 通过 | exit 0 |
| `deep::inner::g(2)`（多级命名空间） | 通过 | exit 0 |
| `deep::inner::RED`（限定枚举成员） | 通过 | exit 0 |
| 命名空间内部写 `nsa::f(2)` | 通过 | exit 0 |
| `int (*p)(int) = nsa::f;` | **失败** | `2:40` |
| `p = nsa::f;` | **失败** | `2:43` |
| `run(nsa::f, 2);`（作实参） | **失败** | `3:35` |
| `int (*p)(int) = &nsa::f;` | **失败** | `2:41` |
| `import nsa::a::f as f;`（函数别名导入） | **失败** | `2:1: the imported unit does not export that name` |
| `static int wrap(int x){ return nsa::f(x); }` 后取 `wrap` | 通过 | exit 0 |
| `struct nsa::Box b;` | **失败** | `2:28: expected a variable name` |
| `nsa::Box b;` | **失败** | `2:27` |
| `import nsa::a;` 后写 `struct Box b;` | 通过 | exit 0 |
| `import nsa::a::Box as Box;` 后写 `struct Box b;` | 通过 | exit 0 |

失败时的完整报错（值位置样例）：

```
v_init.ct:2:40: error: no variable of that name in that namespace (qualified type names are not supported; use the bare name or an alias import)
```

- **归纳**：名字后是否紧跟 `(` 决定走调用解析还是类型/变量解析；两条路径对命名空间前缀的处理不同，且取址路径不做函数符号解析（§3.7）。按位置规则判断即可。
- **补充**：函数不支持 `as` 别名导入（类型支持），所以报错信息里"use … an alias import"对函数并不适用，只能用 wrapper。
- **影响**：桥接层把"命名空间里的函数"当回调/函数指针注册时，必须加 wrapper；跨单元引用类型时用别名 import 或裸名 + `struct`，不要写限定类型名。

### 3.6 其他实测发现（非用户清单，但影响方案）

| 发现 | 后果 |
| --- | --- |
| `trait T { … };` 末尾分号报 `expected a type` | 写 trait 不要带尾分号（struct 可以带） |
| 单文件 `ct compile x.h.ct` 的头是空壳 | 必须用 `ct build --emit-c` 才能得到可用 ABI 头 |
| `ct check` 只接受单文件、遇 `import` 直接报 `import is not supported in compile mode` | 多单元工程无法用 `ct check` 做语义门禁，只能靠 `ct build` + `cc -Werror` |
| `#ct-raw` 中声明的**变量**对 CT 不可见 | 线程/TLS/全局状态必须封在 raw 函数里 |
| `ct check` 不做签名一致性检查 | impl 与 trait 签名漂移由 C 编译器报 `incompatible function pointer types` |
| 生成物逐字节可重现 | 两次 build `diff -r` 为空，可直接做确定性门禁 |
| `--emit-map` 覆盖 trait / struct / function / impl，含源文件行号 | 可作为"符号可追溯"的证据源 |
| `#line` 在 build 模式默认开，`--no-line` 关闭，`--line` 在 compile 模式强制开 | 审阅可用两份产物：带行号版 + 净版 |

### 3.7 【P0】导入函数的裸标识符不被改写：CT 成功，产物不可编译，零诊断

**触发**：单元 `import a::b::mod;` 后，用**裸标识符** `f` 参与取址或调用，而不是写 `a::b::mod::f`。

**实测矩阵**（ct v0.19.1 @ c27da3d，`ct build --emit-c`，macOS arm64）：

| 消费方 | 源码 | CT 退出 / 诊断 | 生成的 C | cc |
| --- | --- | --- | --- | --- |
| 根（无 namespace） | `Fn p = &f;` | **0 / 无** | `Fn p = (&f);` | `error: use of undeclared identifier 'f'` |
| 根 | `f(1);` | 1 / `that function lives in another namespace and needs a qualified call` | — | — |
| 命名空间 `x` | `f(1);` | **0 / 无** | `f(1)` | `error: call to undeclared function 'f'` |
| 命名空间 `x` | `Fn p = &f;` | 1 / `use of an undeclared variable` | — | — |
| 根 | `a::b::mod::f(1);` | 0 / 无 | `a_b_mod_f(1)` | 通过 |
| 根 | `&a::b::mod::f` | 1 / `no variable of that name in that namespace (… use the bare name or an alias import)` | — | — |
| 命名空间 `a` / `a::b` | `mod::f` / `b::mod::f` / `a::b::mod::f` | 1 / `no function of that name in that namespace` | — | — |
| 命名空间 `a::b` | `a_b_mod_f(1)`（直接写 C 名） | 0 / 无 | `a_b_mod_f(1)` | 通过 |
| 任意 | `&g_var`（导入变量） | 0 / 无 | `&a_b_mod_g_var` | 通过 |
| 任意 | `&local_fn` / `local_fn(…)`（同单元） | 0 / 无 | `&local_fn` | 通过 |

**变体**（同一根因，全部 exit 0、零诊断，生成的 C 原样保留 `&f`）：

```c
Fn p = &f;                    /* 全局初始化   -> Fn p = (&f); */
p = &f;                       /* 赋值        -> p = (&f); */
struct S s = { &f };          /* 结构体初始化 -> { &f } */
Fn arr[2] = { &f, 0 };        /* 数组初始化   -> { &f, 0 } */
Fn p = c ? &f : 0;            /* 三元        -> (c ? (&f) : 0); */
run(&f, 1);                   /* 实参        -> run((&f), 1); */
```

**判据**：`ct build --emit-c` 的 CT 阶段退出码为 0，但 `cc` 报 `use of undeclared identifier 'f'`；完整 `ct build`（不带 `--emit-c`）把 cc 作为子步骤执行，因此退出码为 1——失败来自 cc，不是 CT。

**根因**（从行为反推）：函数**取址/引用**路径没有走符号解析，只做原样发射；函数**调用**路径在根单元走了解析（所以硬报错），在命名空间单元却直接透传裸名。变量取址走了解析，所以 `&g_var` 正确。

**为什么定 P0**：静默错误生成把失败推迟到完全不同的阶段。只看 `ct build --emit-c` 退出码的脚本会认为构建成功。

**规避**：

1. 导入函数不得以裸名参与取址或调用；
2. 取址一律写同单元 wrapper（`static int wrap(int x) { return a::b::mod::f(x); }` 仅在根单元可用；命名空间单元需写 C 名 wrapper）；
3. 命名空间单元调用导入函数：直接写生成的 C 名 `a_b_mod_f(…)`（实测可行），或包 wrapper；
4. CI 必须对生成 C 跑 `cc -std=c99 -Wall -Wextra -Werror`，`-Werror` 会把这两类错误都拦下。

**关于 `ct compile` 模式**：compile 模式不接受 `import`，复现不了"导入函数"场景。单文件带 `namespace` 时函数**不**做 mangling（定义 `int f`、调用 `f(1)`、`&f` 三者一致，`&f` 不错译）；但全局变量会被 mangling 而其引用不改写（定义 `a_Fn a_p`，`main` 里仍是 `p(1)`），这是另一条独立的不一致，不计入本条缺陷。

### 3.8 两条命名空间规则的适用范围

| 规则 | 报错文本 | 触发条件 | 实测 |
| --- | --- | --- | --- |
| A 全限定重复当前命名空间 | `no function of that name in that namespace` | 单元 ns=`a`（或 `a::b`）内写 `a::b::mod::f` / `b::mod::f` / `mod::f` | exit 1 |
| B 跨命名空间裸调用 | `that function lives in another namespace and needs a qualified call` | **根单元**裸写 `f(1)`，而 `f` 只存在于已导入的其它命名空间 | exit 1 |

**关键差异**：规则 B 只在**根单元**生效。命名空间单元里同样的裸调用**不报 B**，而是 exit 0 并原样发射 `f(1)`（§3.7 的 P0）。规则 A 对根单元不存在（根单元的前缀就是绝对路径）。

**生成器该按哪条走**：

1. 两条规则都保留，但必须收口到**同一个函数符号解析器**：裸名 = 当前命名空间 → 根；命中唯一的外部命名空间时，根单元按 B 硬报错，命名空间单元**应改写为该函数的 cname**（`a_b_mod_f`）而不是原样发射。
2. 限定名解析应改为"先绝对、后相对"，或干脆不允许源码写限定名、由生成器直接输出 cname；当前"相对当前命名空间"的实现使命名空间单元无法引用导入的嵌套命名空间函数。
3. 取址/引用路径必须复用调用路径的解析器，否则 §3.7 的静默错译会在其它位置复现。

---

## 4. 对桥接层的硬约束（由以上事实直接推出）

1. **ABI 头只能由 `ct build --emit-c` 生成**，Swift module map / JNI 头都引用这一份；禁止手写第二份原型。
2. **跨语言类型只用定宽整数、不透明指针、纯字段 struct**；带 `use Trait` 的 struct 不出现在 ABI 边界（或只以 `void *` 形式出现）。
3. **trait 接口值一律视为借用**。跨越返回/队列/线程边界的必须指向堆对象并有显式 owner；`make_local` 式返回按 P0 处理。
4. **回调注册必须经 wrapper**：`ns::fn` 不能作值；wrapper 定义在调用方单元或根命名空间。
5. **并发完全自建**：pthread + condvar + 有界队列 + 线程归属断言，全部封装在 `.shim.h/.c` 或 raw 函数里。
6. **`errno` 不可用**：所有 C 库错误经 shim 转成返回值/显式 out 参数。
7. **禁止在 `.ct` 里写 `#ifdef`**：条件编译下沉到 shim 头。
8. **`import` 全部前置**，并加 lint。
9. **QuickJS 的引用计数由桥接层手工管理**；`JSValue` 不跨线程、不长期驻留在 C 结构里（除非有明确 free 路径）。
10. **CI 至少三条硬门禁**：`ct build` 成功 + `cc -Wall -Wextra -Werror` 零警告 + 两次构建产物 `diff` 为空；再加 ASan 跑生命周期用例。
11. **导入函数禁止裸名**：取址一律 wrapper；调用在根单元写绝对限定名，在命名空间单元写生成的 C 名或 wrapper（§3.7）。
12. **构建脚本不得只看 `ct build --emit-c` 的退出码**：必须把生成的 C 交给 `cc -Werror`；否则 `&f` 这类静默错译会被当成成功。

---

## 5. 未验证项（不要当结论用）

| 项 | 原因 |
| --- | --- |
| 与真实 QuickJS 源码/静态库的编译链接 | 本次核查刻意不读 `experiments/`（编码角色产出，含 QuickJS 副本）；仅按 API 形状做了 stub 验证 |
| `JS_ExecutePendingJob`、GC、`JS_SetInterruptHandler` 行为 | 同上 |
| Swift 通过 module map 直接消费生成头的实际编译 | 未建 Xcode 工程；头是标准 C99，理论上可用 |
| Kotlin/JNI 侧签名与 `jlong`/`jint` 映射 | 未建 Android 工程 |
| Linux/Android 上的 `long` 宽度与结构体 ABI | 实验在 macOS arm64；结论中的"定宽类型"要求可规避 |
| `_Atomic` 在 CT 语义检查下的边界（是否有未覆盖写法） | 只验证了常见写法 |
| 大结构体按值跨 ABI 的寄存器/内存分类 | 只验证了 8/16 字节结构 |
| `ct build` 的增量缓存对 `.shim` 变更的重建正确性 | 只跑了一次性构建 |

---

## 6. 复现附录（实验命令）

所有实验在 `/tmp/ctreview` 下完成，可按顺序重放：

```bash
# 基线
ct -v                      # ct v0.19.1
git -C /Users/samxx/vscspace/ct rev-parse --short HEAD   # c27da3d

# A. 导出头（多文件模式才完整）
ct build --emit-c --ns-root . bridge/core.h.ct -o bout
cat bout/core.h

# A. dylib 与符号
ct build --emit-c --ns-root . bridge/core.h.ct -o dyl
cc -std=c99 -fPIC -shared -I dyl dyl/core.c -o dyl/libnxcore.dylib
nm -gU dyl/libnxcore.dylib | grep bridge_

# B. C → CT 回调（需自写 bout/main.c）
cc -std=c99 -Wall -Wextra -I bout bout/core.c bout/main.c -o bout/demo && ./bout/demo

# C/E. trait 布局与悬垂
ct traitdemo.ct -o traitdemo.c
cc -std=c99 -fsanitize=address -g traitdemo.c -o traitdemo
ASAN_OPTIONS=detect_stack_use_after_return=1 ./traitdemo

# D. 类型静默接受
ct check typecheck2.ct     # 只有 f_aval(struct) 一条

# F. 线程/原子/TLS
ct threads.ct -o threads.c
cc -std=c99 -Wall -Wextra -pthread threads.c -o threads && ./threads

# G. QuickJS 形状
ct qjs/glue.ct -o qjs/glue.c
cc -std=c99 -Wall -Wextra -I qjs qjs/glue.c qjs/stub.c -o qjs/glue && ./qjs/glue

# 缺口
ct build --emit-c -I gap --ns-root gap gap/plain_import.ct -o gap/o1   # only a .h.ct unit …
ct build --emit-c --ns-root . gap/late_import.ct -o gap/o2             # 静默丢弃 import
ct errno_test.ct -o /tmp/e.c                                           # use of an undeclared variable
ct ifdef_test.ct -o /tmp/i.c                                           # 两个分支都发射
ct build --emit-c -I nsq --ns-root . qfnptr2.ct -o qo7                 # qualified type names are not supported

# H. 可审阅性
ct build --emit-c --ns-root . bridge/core.h.ct -o det1
ct build --emit-c --ns-root . bridge/core.h.ct -o det2
diff -r det1 det2 && echo DETERMINISTIC
ct compile abi2/core.h.ct --emit-map -
```

---

## 7. 复核补证（2026-11-06 第二次独立复现）

> 触发原因：与编码角色在"隐藏 vptr"和"`ns::fn` 解析"两处结论冲突。以下为重新构造的最小实验，未读取 `experiments/`。基线：`ct v0.19.1`，commit `c27da3dfde46a24090e0cedc2954d76f8d104c07`，macOS arm64。全部源码在 `/tmp/ctrepro`。

### 7.1 `use Trait` 的隐藏 vptr

`/tmp/ctrepro/layout/one_trait.ct`（节选，完整文件可照抄复现）：

```c
trait Greeter { void greet(); }

struct Person {
    char *name;
    int age;
} use Greeter

impl Greeter for Person { void greet() { } }

int main(void) {
    printf("one-trait sizeof=%zu name=%zu age=%zu\n",
           sizeof(struct Person), offsetof(struct Person, name), offsetof(struct Person, age));
}
```

命令与实测输出：

```
$ ct layout/plain.ct -o layout/plain.c && cc -std=c99 layout/plain.c -o layout/plain && ./layout/plain
plain sizeof=16 name=0 age=8

$ ct layout/one_trait.ct -o layout/one_trait.c && cc -std=c99 layout/one_trait.c -o layout/one_trait && ./layout/one_trait
one-trait sizeof=24 name=0 age=8

$ ct layout/two_traits.ct -o layout/two_traits.c && cc -std=c99 layout/two_traits.c -o layout/two_traits && ./layout/two_traits
two-traits sizeof=24 x=0 y=4

$ ct check layout/one_trait.ct; echo check_exit=$?
check_exit=0
```

生成的 C（`layout/one_trait.c:16-20`）：

```c
struct Person {
    char * name;
    int age;
    struct Greeter_vtbl *__greeter_vptr;
};
```

生成的 C（`layout/two_traits.c:25-30`）：

```c
struct Pair {
    int x;
    char y;
    struct First_vtbl *__first_vptr;
    struct Second_vtbl *__second_vptr;
};
```

导出头（`ct build --emit-c --ns-root . exp/export.h.ct -o exo` → `exo/export.h`，原件）：

```c
struct Person;
struct Greeter_vtbl {
    void (*greet)(void *);
};
struct Greeter_iface {
    void *self;
    struct Greeter_vtbl *vt;
};

extern struct Greeter_vtbl Person_Greeter_vtable;

struct Person {
    char * name;
    int age;
    struct Greeter_vtbl *__greeter_vptr;
};
```

用 C 探针 `#include "export.h"` 实测：`header sizeof=24 name=0 age=8 greet_vptr=16`。

**结论**：初稿成立。`Person` 16→24；两个 trait = 2 个 vptr（`Pair` 从 8 变 24）；`ct check` 全部零诊断。编码角色若得到"与手写 C 逐行一致"，应是不带 trait 的结构体，或镜像时已手动加上 vptr。

### 7.2 `ns::fn` 值位置

`/tmp/ctrepro/v_init.ct`（原样）：

```c
import nsa::a;
int main(void) { int (*p)(int) = nsa::f; return p(1); }
```

```
$ ct build --emit-c -I nsq --ns-root . v_init.ct -o out/v_init
v_init.ct:2:40: error: no variable of that name in that namespace (qualified type names are not supported; use the bare name or an alias import)
$ echo $?
1
```

调用位置对照（`v_call.ct` 内容 `int x = nsa::f(1);`）：`exit=0`。

**结论**：值位置失败的结论成立；但把它概括为"`ns::fn(...)` 解析问题"不准确——调用形式按函数解析，失败只发生在不跟 `(` 的位置（规则见 §3.5）。命名空间单元内的前缀解析另有独立缺陷，见 §3.8。

### 7.3 import 方向

```
# 普通 .ct 作为导入方（build 模式）
$ ct build --emit-c -I nsq --ns-root . v_call.ct -o out/v_call2
cc -std=c99 out/v_call2/v_call.c out/v_call2/a.c -o a.out
$ echo $?
0

# 普通 .ct 作为被导入方
$ ct build --emit-c -I . --ns-root . imp_plain.ct -o out/imp_plain
ctc: only a .h.ct unit can be imported: rename 'helper.ct' to 'helper.h.ct'
$ echo $?
1

# compile 模式不接受 import
$ ct compile imp_plain.ct -o imp_plain.c
imp_plain.ct:1:1: error: 'import' is not supported in compile mode
$ echo $?
1

# .m.ct 可被导入
$ ct build --emit-c -I nsq --ns-root . imp_m.ct -o out/imp_m
cc -std=c99 out/imp_m/imp_m.c -o a.out
$ echo $?
0
# out/imp_m/imp_m.c 中：return cput((65));
```

**结论**：初稿指的是"普通 `.ct` 不能作为**被导入方**"；作为**导入方**，它在 `ct build` 下正常。初稿漏写了 `ct compile` 完全不接受 import 这一限制。已在 §3.1 更正。

### 7.4 复核补证：`&f` 静默错译（P0）

被导入单元 `/tmp/ctrepro2/nsq/a/b/mod.h.ct`：

```c
namespace a::b::mod;
pub int f(int x) { return x + 1; }
pub int g_var = 7;
```

根单元 `/tmp/ctrepro2/t1_and_global.ct`：

```c
import a::b::mod;
typedef int (*Fn)(int);
Fn p = &f;
int main(void) { return p(1); }
```

```
$ ct build --emit-c -I nsq --ns-root . t1_and_global.ct -o out/stderrchk
cc -std=c99 out/stderrchk/t1_and_global.c out/stderrchk/mod.c -o a.out
$ echo $?
0
$ # stderr 字节数
$ wc -c < /tmp/sc.err
0

$ grep -n '&f' out/stderrchk/t1_and_global.c
3:Fn p = (&f);
$ grep -n 'int a_b_mod_f' out/stderrchk/mod.c
5:int a_b_mod_f(int x) { return (x + 1); }

$ cc -std=c99 out/stderrchk/t1_and_global.c out/stderrchk/mod.c -o t1bin
out/stderrchk/t1_and_global.c:3:10: error: use of undeclared identifier 'f'
$ echo $?
1
```

注意：`--emit-c` **只打印**那条 cc 命令，不执行（目录里没有 `a.out`，只有 `.c/.h`）。因此只看 `--emit-c` 退出码的构建会误判成功。

命名空间单元里的同类缺陷（裸调用）：

```c
// nsq/x/barecall.h.ct
namespace x;
import a::b::mod;
pub int call_bare(void) { return f(1); }
```

```
$ ct build --emit-c -I nsq --ns-root . nsq/x/barecall.h.ct -o out/x_barecall_unit
$ echo $?      -> 0
$ grep -n 'f(1)' out/x_barecall_unit/barecall.c
5:    return f(1);
$ cc -std=c99 out/x_barecall_unit/barecall.c out/t1_and_global/mod.c -o xb
error: call to undeclared function 'f'; ISO C99 and later do not support implicit function declarations
```

对照组（全部正确）：

```
&g_var（导入变量）        -> &a_b_mod_g_var
&local_fn（同单元 static） -> &local_fn
根单元 a::b::mod::f(1)    -> a_b_mod_f(1)
命名空间单元 a_b_mod_f(1)  -> a_b_mod_f(1)
```

`ct compile` 模式：`import` 直接报 `'import' is not supported in compile mode`，无法复现导入函数场景；单文件带 `namespace` 时函数不 mangling，`&f` 与定义一致、不错译（全局变量另有 mangling 不一致，见 §3.7 末段）。
