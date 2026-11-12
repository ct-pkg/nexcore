# Schema 与代码生成

## 1. 唯一事实源

`schema/nexcore.schema.yaml` 是**宿主项目**的方法、事件、类型、方向、超时、错误策略的唯一事实源。文件内容由宿主定义；框架交付的是生成器 CLI、Schema 形态与生成规则，不交付某一份业务 Schema。本文中的 YAML 均为形态示例/测试 fixture。任何手写绑定、手写方法名常量、手写 JSON 键都属于漂移，CI 必须拒绝。

生成物：

| 产物 | 路径 | 消费者 |
|---|---|---|
| C 注册代码 | `schema/generated/c/nexcore_registry.{h,c}` | Bridge、Core |
| Swift 绑定 | `schema/generated/swift/NexCoreAPI.swift` | macOS/iOS UI |
| Kotlin 绑定 | `schema/generated/kotlin/NexCoreApi.kt` | Android UI |
| JS 命名空间 | `schema/generated/js/nexcore.api.js` | JS 业务代码 |
| 清单 | `schema/generated/manifest.json` | CI、启动校验、审计 |

生成物全部提交到**宿主项目**仓库；框架仓库只保留生成器、模板与测试 fixture。CI 重新生成并逐字节比对，任何漂移直接失败。

### 1.1 当前阶段的生成器输入：`src/units.json`（不是 YAML）

P1–P2 的编码骨架先于 Schema 管线落地，而当前环境没有 PyYAML，因此生成器的输入暂时改为**单元/符号清单** `src/units.json`。当前实际形态（`source` 相对 `--src`）：

```json
{
  "units": [
    {
      "namespace": "queue",
      "trait": "Ring",
      "source": "queue/queue.h.ct",
      "methods": ["open", "push", "depth", "close"]
    },
    {
      "namespace": "js_host",
      "trait": "Host",
      "source": "js_host/js_host.h.ct",
      "methods": ["init", "shutdown", "turns"]
    }
  ]
}
```

- `units` 是**需要跨单元映射的 trait 方法清单**，不是全部 CT 单元；recipe key 由 `(namespace, trait, method)` 直接给出。
- **双源风险由一致性检查消除（强制）**：每个 unit 的 `source` `.h.ct` 里的**所有 `pub` 函数名**必须与该 unit 的 `methods` 集合**完全相等**。`tools/check.sh` 的附加检查 **⑤b**（由编码侧落脚本，需求侧只给检查需求）负责解析 `.h.ct` 与 `units.json` 并比对；生成器只遍历 `methods`，漏列会静默不生成 recipe，`--check` 也不会报，所以必须由 ⑤b 拦截。规则：需要跨单元映射的函数列入 `methods`；纯内部函数不要写成 `pub`。**⑤b 已随 `tools/check.sh` 全量实跑通过**（真实 `SCRIPT_RC=0` + 逐项 PASS，含跨行声明与双向差集负例）。
- `cname` **不写在 `units.json`**：由生成器按 CT 的命名规则产出，并由 `--verify-ct build/impl` 与导出目录逐条比对；手写 cname 视为漂移。
- 目录/命名空间/import 的事实由 `tools/check.sh` 的 **⑤a** 检查，不在 `units.json` 重复维护；`units.json` 只回答"哪些 trait 方法需要映射"。**⑤a 已随 `check.sh` 全量实跑通过**（含普通 `foo_map.ct` 的负例）。
- 输入是 JSON 而不是 YAML 的原因只有两个：**环境无 PyYAML**；**P3 之前没有业务 Schema**。这不是长期形态，记录在 D-06 的"重审"里。
- **P3 接入 Schema 后不回到"只用 YAML"**：业务 Schema 恢复为 YAML（宿主的方法/事件/类型事实源），`src/units.json` 继续作为 **CT 映射清单**保留，因为命名空间、trait、method 这些事实无法从业务 Schema 推导。生成器届时同时接受 `--spec src/units.json`（必需）与 `--schema schema/nexcore.schema.yaml`（可选；缺省只生成桥接层符号相关产物）。两者职责分开，不做"YAML 里再嵌一份单元清单"的双写。
- 在下文与 §2 中出现的 YAML 都是 **P3 的目标形态**；当前阶段只跑 `units.json` 的符号表与映射单元生成。


## 2. Schema 形态

### 2.1 示例

```yaml
schema: 1
package: com.example.nexcore
namespace: shop
revision: 2026-11-06.1

types:
  CartLine:
    kind: record
    doc: 购物车一行
    fields:
      sku:
        type: int32
        min: 1
      qty:
        type: int32
        min: 1
        max: 999
      note:
        type: optional<string>
        max_length: 200

  Money:
    kind: record
    fields:
      cents:
        type: int64
        min: 0
      currency:
        type: string
        enum: [CNY, USD, EUR]

  Theme:
    kind: enum
    values: [light, dark, system]

methods:
  cart.add:
    side: core
    doc: 加入购物车
    c_handler: cart_add
    params:
      line:
        type: CartLine
    result:
      type: Money
    timeout_ms: 2000

  cart.list:
    side: core
    c_handler: cart_list
    params: {}
    result:
      type: list<CartLine>
    timeout_ms: 2000

  ui.toast:
    side: ui
    doc: 显示原生 toast
    params:
      text:
        type: string
        max_length: 200
      duration_ms:
        type: int32
        default: 2000
        min: 0
        max: 10000
    result:
      type: null
    timeout_ms: 1000
    cancellable: false

  app.refresh:
    side: js
    doc: UI 触发一次刷新
    params:
      force:
        type: bool
        default: false
    result:
      type: int32
    timeout_ms: 3000

events:
  cart.changed:
    doc: 购物车内容变化
    payload:
      type: list<CartLine>
    emit_from: [core, js, ui]
    delivery: reliable

  ui.scroll:
    payload:
      type: float64
    emit_from: [ui]
    delivery: latest
```

### 2.2 约束

- `schema` 固定为 `1`；变更结构才升。
- `package` 用于 Swift module / Kotlin package；`namespace` 用于 JS 对象与方法前缀。
- `revision` 只给人看；机器校验用 `manifest.json` 的 SHA-256。
- 方法名与事件名必须满足 protocol.md §3.2 的命名规则；一个名字只能出现在 `methods` 或 `events` 之一。
- `side` 只能取 `core | ui | js`：
  - `core`：只能被 JS 调用；必须有 `c_handler`。
  - `ui`：只能被 JS 调用；由平台实现生成的 protocol/interface。
  - `js`：只能被 native 调用；由 JS 实现生成的 handler。
- `params` 必须是对象类型（record 或空）；`result` 可以是任意类型或 `null`。
- `timeout_ms` 必填或使用默认 5000；`0` 表示不超时，必须显式写出并说明理由。
- `cancellable: true` 时生成取消标志参数；默认 `false`。
- `idempotency_key: true` 时在 params 中生成 `idempotency_key: string` 字段，框架不自动去重，只保证键的存在与长度。
- 事件必须有 `emit_from` 与 `delivery`；`delivery: latest` 只允许用于可丢弃的状态类事件。
- 不支持 `oneOf`/任意 union；用 record + optional 字段表达。v2 再评估。

### 2.3 类型系统

| Schema 类型 | Wire（JSON） | C（生成） | Swift | Kotlin | JS |
|---|---|---|---|---|---|
| `bool` | boolean | `bool` | `Bool` | `Boolean` | `boolean` |
| `int32` | number | `int32_t` | `Int32` | `Int` | `number` |
| `int64` | number，±(2^53-1) | `int64_t` | `Int64` | `Long` | `number`（超出报错） |
| `float64` | number，有限 | `double` | `Double` | `Double` | `number` |
| `string` | string，UTF-8 | `NxStr` | `String` | `String` | `string` |
| `bytes` | Base64 string | `NxBytes` | `Data` | `ByteArray` | `Uint8Array` |
| `null` | null | 无 | `Void` | `Unit` | `null` |
| `list<T>` | array | `NxList_<T>` | `[T]` | `List<T>` | `T[]` |
| `map<string,T>` | object | `NxMap_<T>` | `[String: T]` | `Map<String, T>` | `Record<string, T>` |
| `optional<T>` | 缺失或 null | `bool has_<f>` + 值 | `T?` | `T?` | `T \| undefined` |
| `record` | object | `Nx<Name>` | `struct` | `data class` | object |
| `enum` | string | `enum Nx<Name>` | `enum` | `enum class` | string union（JSDoc） |

- `NxStr` 是 `{ const char *ptr; size_t len; }`，不保证 NUL 结尾；字符串比较必须带长度。
- `NxBytes` 是 `{ const uint8_t *ptr; size_t len; }`；解码时分配在 arena。
- `int64` 在 JS 侧超过安全整数直接 `E_INVALID_PARAMS`；不要用 `BigInt` 隐式转换。
- `optional<T>` 在 C 中显式生成 `has_<field>`，禁止用哨兵值。
- 枚举值必须是合法标识符；生成时按 Schema 顺序输出，不排序，避免语义顺序变化。

### 2.4 跨语言数据形状

这里必须把界线写死，否则 trait 的隐藏 vptr 会让平台二进制在无声中错位。

| 数据 | 形状 | 是否跨 ABI | 规则 |
|---|---|---|---|
| UI↔JS 的业务参数/结果/事件 | JSON 字节/字符串 | 是 | 唯一业务数据边界；不受 CT 结构体布局影响 |
| `struct NexCoreConfig` | 冻结的 C 结构体（只有 tag，无 typedef 别名） | 是 | 不含 `use Trait`；`struct_size` + `abi_version`；`_Static_assert` 锁定 offset/size；C 侧必须写 `struct NexCoreConfig`（`pub typedef struct X X;` 实测报 `unexpected token`；正确的不透明声明是 `pub struct X;`） |
| 句柄 | `void *` typedef | 是 | 不透明；不导出内部布局 |
| Schema 生成的 `NxCartLine` 等 | CT/C 内部结构体 | **否** | 只用于 Core 与 JSValue 编解码；不得出现在 `NexCore.h` |
| Trait / `use Trait` 结构体 | 带隐藏 vptr | **绝对否** | 递归检查：按值嵌套也会传染；生成器遇到 `use` 直接失败 |

生成器必须做两件事：

1. 扫描所有 `pub struct`，出现 `use` 立即失败；
2. 递归检查生成 C 类型的字段类型链，任何按值出现的带 trait 类型都失败（指针嵌套允许，但本项目仍不使用）。

原因（实测）：`struct Person { char *name; int age; }` 是 16 字节，加一个 `use Greeter` 变 24；`Holder` 按值嵌套带 trait 的 `One` 会从 24 变 40。平台侧按旧头文件编译后，只要 CT 侧后来加了一个 trait，偏移和 `sizeof` 就全部错位，且 `ct check` 零诊断。

## 3. 生成器

### 3.1 CLI

```bash
# 当前阶段（P1–P2）：只生成/校验映射单元（<ns>/<ns>_map.m.ct），写回 src/
python3 tools/nexcore_gen.py --spec src/units.json --src src
python3 tools/nexcore_gen.py --spec src/units.json --src src --check
python3 tools/nexcore_gen.py --spec src/units.json --src src --verify-ct build/impl

# P3 接入 Schema 后（规划）：`--spec` 仍必需，`--schema` 追加业务 Schema，`--out` 指定生成树
nexcore-gen \
  --spec src/units.json \
  --schema schema/nexcore.schema.yaml \
  --out schema/generated \
  --check
```

退出码：`0` 成功；`1` 输入非法（JSON/YAML/Schema 任一）；`2` 生成物漂移（`--check`）；`3` 内部错误。`--spec` 缺失或文件不存在直接退出 `1`，不做隐式扫描目录。

### 3.2 实现

- Python 3.10+。**当前阶段不依赖 PyYAML**（环境没有）：`units.json` 用标准库 `json` 读取；只有 P3 的 `--schema` 分支才需要 PyYAML，届时把它作为可选依赖并写入 `requirements-dev.txt` 锁定 hash。生成器是**开发工具**，不进入 App、不进入 CT 产物。
- 备选：后续用 CT 自举重写；在重写前，Python 版本的 golden 测试是规范。
- 生成器是纯函数：输入 = `units.json`（+ P3 的 schema 文件）+ 生成器版本 + 输出配置；输出 = 文件字节。不得读取环境变量、时间、用户名、网络、随机数。
- **生成器必须先建符号表再发射代码**：输入包含所有 `.ct` 单元清单、每个单元的 `namespace`、`import`、函数/变量定义与所属单元。发射任何标识符前都走符号解析（见 §8.1 SYM-01/SYM-02）；不允许“原样输出 Schema 里的名字，交给 CT 解析”。
- 所有输出按固定顺序：
  - 类型按名字排序；
  - 方法按名字排序；
  - 字段按 Schema 声明顺序；
  - 枚举值按 Schema 顺序；
  - 导入/声明按固定模板。
- 每个文件头：

```
// GENERATED FROM src/units.json [+ schema/nexcore.schema.yaml]
// input-sha256: <64 hex>
// generator: nexcore-gen <version>
// DO NOT EDIT.
```

### 3.3 校验

生成前必须拒绝：

1. 未知字段、未知类型、拼写错误（`side: cores`）。
2. 方法名/事件名重复，或与保留 `$` 前缀冲突。
3. `core` 方法缺 `c_handler`，或 `c_handler` 不符合 C 标识符规则。
4. `optional<optional<T>>`、`list<null>`、`map<string, null>` 等无意义组合。
5. `timeout_ms` 缺失且无默认；`0` 但没有 `reason`。
6. `delivery: latest` 用于带 `idempotency_key` 或 `cancellable` 的事件。
7. 枚举值重复、空字符串、非标识符。
8. 默认值类型不匹配、超出 min/max。
9. 破坏性变更但未升 `protocol_version`（见 §9.2）。

错误信息必须包含文件、行号、字段路径，例如：

```
schema/nexcore.schema.yaml:42: methods.cart.add.params.line.type: unknown type 'CartLine2'
```

## 4. 生成的 C 注册代码

### 4.1 结构

`nexcore_registry.h`：

```c
#include "NexCore.h"           /* ABI 常量（pub enum）只从这里来，不重复定义 */

#define NX_SCHEMA_SHA256 "<64 hex>"
#define NX_METHOD_COUNT 3
#define NX_EVENT_COUNT 2
/* 不再生成 NX_ABI_VERSION：直接用 NEXCORE_ABI_VERSION */

typedef struct NxMethodDesc {
    const char *name;
    uint8_t side;          /* NX_SIDE_CORE/UI/JS */
    uint8_t direction;     /* NX_DIR_UI2JS/JS2UI */
    uint8_t flags;
    uint16_t timeout_ms;
    void *handler;         /* core: JSValue (*)(...); ui: NULL */
} NxMethodDesc;

extern const NxMethodDesc nx_methods[NX_METHOD_COUNT];
extern const NxEventDesc nx_events[NX_EVENT_COUNT];

int nx_register_generated(JSContext *ctx);
```

`nexcore_registry.c`：

```c
#include "core_api.h"          /* 手写，声明 Core 实现 */
#include "nexcore_registry.h"

/* ---- cart.add ---- */
static JSValue nx_core_cart_add(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    NxCartLine line;
    NxMoney out;
    NxArena *a;
    (void)this_val;

    if (argc != 1) return nx_throw(ctx, "E_INVALID_PARAMS", "cart.add expects 1 argument");
    if (nx_arg_decode_CartLine(ctx, argv[0], &line) != 0) return JS_EXCEPTION;

    a = nx_arena_new();
    if (a == NULL) return nx_throw(ctx, "E_INTERNAL", "arena allocation failed");
    if (cart_add(a, &line, &out) != 0) {
        nx_arena_free(a);
        return nx_throw(ctx, "E_INTERNAL", "cart_add failed");
    }
    JSValue v = nx_encode_Money(ctx, &out);
    nx_arena_free(a);
    return v;
}
```

注册函数必须逐条写出，不用宏循环：

```c
int nx_register_generated(JSContext *ctx) {
    JSValue core = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, core, "cart.add",
        JS_NewCFunction2(ctx, nx_core_cart_add, "cart.add", 1, JS_CFUNC_generic, 0));
    /* ... 每个 core 方法一行 ... */
    JS_SetPropertyStr(ctx, global, "nexcore_core", core);
    return 0;
}
```

### 4.2 Core handler 约定

生成的包装函数调用手写 Core 函数，签名固定：

```c
/* 无返回值 */
int32_t cart_clear(NxArena *a);
/* 有返回值 */
int32_t cart_add(NxArena *a, const NxCartLine *line, NxMoney *out);
/* 多参数 */
int32_t cart_update(NxArena *a, int32_t sku, int32_t qty, NxMoney *out);
```

规则：

- 第一个参数永远是 `NxArena *`；所有输出字符串/数组从 arena 分配。
- 输入标量按值；输入 record/string/bytes 用 `const T *`（字符串用 `const NxStr *`）。
- 输出用最后一个 `T *out`；无返回值则没有。
- 返回 `0` 成功；非 0 是 Core 错误码，包装函数映射为 JS 异常。
- Core 不得 `malloc` 后要求 Bridge 释放；不得保存 `NxStr` 指针到调用之后。
- `NxArena` 由 Bridge 提供；Core 只调用 `nx_arena_alloc`。

### 4.3 显式解码/编码

每个 record/枚举生成：

```c
int nx_arg_decode_CartLine(JSContext *ctx, JSValueConst v, NxCartLine *out);
JSValue nx_encode_CartLine(JSContext *ctx, const NxCartLine *v);
```

实现逐字段写：

```c
int nx_arg_decode_CartLine(JSContext *ctx, JSValueConst v, NxCartLine *out) {
    JSValue x;
    if (!JS_IsObject(v)) return nx_fail(ctx, "E_INVALID_PARAMS", "CartLine must be an object");

    x = JS_GetPropertyStr(ctx, v, "sku");
    if (JS_ToInt32(ctx, &out->sku, x) != 0) { JS_FreeValue(ctx, x); return -1; }
    JS_FreeValue(ctx, x);

    x = JS_GetPropertyStr(ctx, v, "qty");
    if (JS_ToInt32(ctx, &out->qty, x) != 0) { JS_FreeValue(ctx, x); return -1; }
    JS_FreeValue(ctx, x);
    if (out->qty < 1 || out->qty > 999) return nx_fail(ctx, "E_INVALID_PARAMS", "qty out of range");

    x = JS_GetPropertyStr(ctx, v, "note");
    out->has_note = !JS_IsUndefined(x) && !JS_IsNull(x);
    if (out->has_note) {
        out->note = nx_arg_str(ctx, x);   /* 失败返回 false */
        if (out->note.ptr == NULL) { JS_FreeValue(ctx, x); return -1; }
    }
    JS_FreeValue(ctx, x);
    return 0;
}
```

禁止：

- 用 `JS_GetPropertyStr` 循环遍历未知字段来“自动映射”；
- 用字段名字符串拼接；
- 用 `JS_JSONStringify` 把整个对象再解析成 C（那是另一套隐式转换）。

## 5. 生成的 Swift / Kotlin 绑定

**生成器必须写入的关闭注释（绑定侧原文）**：`shutdown`/`free` 的包装函数上固定生成：

```
/// 只能在非 JS 线程调用 free；在 JS handler/Core handler 内只调用 shutdown 并立即 return，
/// 不要调用 free、不要等待关闭完成。free 由外部线程在 drain 线程退出后执行。
```

这条注释不是可选的文档装饰：生成器模板缺它就视为生成器 bug（golden 比对会失败）。原因与状态机见 async-threading.md §7.1。

### 5.1 Swift

```swift
public struct CartLine: Codable, Sendable {
    public let sku: Int32
    public let qty: Int32
    public let note: String?

    enum CodingKeys: String, CodingKey {
        case sku, qty, note
    }
}

public protocol NexCoreUIHandler: AnyObject {
    @MainActor func toast(text: String, durationMs: Int32) async throws
}

@MainActor
public final class NexCoreClient {
    public init(transport: NexCoreTransport) throws
    public func cartAdd(_ line: CartLine) async throws -> Money
    public func onCartChanged(_ handler: @escaping @MainActor (CartLine) -> Void) -> NexCoreSubscription
    public func setUIHandler(_ handler: NexCoreUIHandler?)
    public func close()
}
```

规则：

- 公开 API 不出现 `Any`、`[String: Any]`、`JSONSerialization`。
- `Codable` 的 `CodingKeys` 显式列出，不用自动 snake_case 转换。
- UI handler 一律 `@MainActor`；生成的 dispatcher 在 drain 线程收到消息后 `Task { @MainActor in ... }`。
- `async` 方法用 `withCheckedThrowingContinuation`；continuation 只能 resume 一次（由 transport 的 pending 表保证）。
- 订阅返回 `NexCoreSubscription`，`cancel()` 幂等。
- 错误是 `NexCoreError`（含 `code`、`message`、`data`），不是字符串。

### 5.2 Kotlin

```kotlin
@Serializable
data class CartLine(
    @SerialName("sku") val sku: Int,
    @SerialName("qty") val qty: Int,
    @SerialName("note") val note: String? = null,
)

interface NexCoreUiHandler {
    suspend fun toast(text: String, durationMs: Int)
}

class NexCoreClient(transport: NexCoreTransport) : Closeable {
    suspend fun cartAdd(line: CartLine): Money
    fun onCartChanged(handler: (List<CartLine>) -> Unit): NexCoreSubscription
    fun setUiHandler(handler: NexCoreUiHandler?)
    override fun close()
}
```

规则：

- `kotlinx.serialization`（编译期插件，无运行时反射）；`@SerialName` 显式写出。
- `suspend` 方法内部用 `suspendCancellableCoroutine`；`invokeOnCancellation` 发送 `$cancel`。
- UI handler 默认在 `Dispatchers.Main.immediate` 执行；生成代码里可见地 `withContext`。
- 不生成 `Flow`（v1 用回调 + Subscription，减少调度语义）；后续可加扩展函数。
- 不生成 `Any`/`JsonElement` 公开参数。
- **Kotlin/Native 未实测**：v1 只验收 Android JNI + Kotlin/JVM；生成物不依赖 JVM 专有 API，但 Kotlin/Native 的 C interop 路径需另行验证（Q-07）。

## 6. 生成的 JS 命名空间

```js
// schema/generated/js/nexcore.api.js
export const SCHEMA_SHA256 = "<64 hex>";

export const core = {
  cart: {
    add: async (line) => __nx_core_call("cart.add", [line]),
    list: async () => __nx_core_call("cart.list", []),
  },
};

export const ui = {
  toast: (text, duration_ms = 2000) => __nx_call(nextId(), "ui.toast", { text, duration_ms }),
};

export const events = {
  cartChanged: {
    subscribe: (fn) => __nx_subscribe("cart.changed", fn),
    emit: (payload) => __nx_emit("cart.changed", payload),
  },
};

export const handlers = Object.create(null);

export function registerHandlers(map) {
  for (const k of Object.keys(map)) {
    if (!(k in HANDLER_NAMES)) throw new Error("unknown handler: " + k);
    handlers[k] = map[k];
  }
}
```

规则：

- 方法名是字符串字面量，来自 Schema；不拼接、不计算。
- `core` 方法用 `async` 包装同步 C 调用，统一 Promise 语义。
- `ui` 方法返回 Promise；`nextId()` 由 bootstrap 提供。
- `events` 的 `subscribe/emit` 是显式函数；不使用 `Proxy`、getter 魔法。
- `handlers` 是 `Object.create(null)`，避免原型污染；`registerHandlers` 校验 key 白名单。
- 生成文件不包含业务实现；业务代码在 `js/` 手写目录，只 import 生成物。
- 上面的 ESM 是**构建期源码形态**：发布前由打包器把 `nexcore.api.js` 与业务 JS 合成单段 `bootstrap_js`，并挂到 `globalThis.nexcore = { core, ui, events, registerHandlers }`。运行时没有模块加载器，不允许 `import()`（Q-17）。

## 7. 命名规则

| Schema | C | Swift | Kotlin | JS |
|---|---|---|---|---|
| 方法 `cart.add` (core) | `nx_core_cart_add` | `cartAdd` | `cartAdd` | `core.cart.add` |
| 方法 `ui.toast` (ui) | `nx_ui_toast`（registry 条目） | `toast` | `toast` | `ui.toast` |
| 方法 `app.refresh` (js) | `nx_js_app_refresh`（registry 条目） | `refresh` | `refresh` | `handlers["app.refresh"]` |
| 类型 `CartLine` | `NxCartLine` | `CartLine` | `CartLine` | JSDoc `CartLine` |
| 字段 `duration_ms` | `duration_ms` | `durationMs` | `durationMs` | `duration_ms` |
| 枚举 `Theme` 值 `dark` | `NX_THEME_DARK` | `.dark` | `Theme.DARK` | `"dark"` |
| 事件 `cart.changed` | `NX_EVENT_CART_CHANGED` | `onCartChanged` | `onCartChanged` | `events.cartChanged` |

- 名称冲突（例如 `cart.add` 与 `cart_add` 都映射到 `cartAdd`）是**生成错误**，不自动加后缀。
- 平台保留字冲突也是生成错误，Schema 作者改名；不做隐式转义。
- 字段 wire 名与 Schema 完全一致；平台属性名按语言惯例（camelCase），但通过显式 `CodingKeys`/`@SerialName` 映射，代码可见。

## 8. “无魔法、可逐行审阅”的可检查规则

| ID | 规则 | 检查 |
|---|---|---|
| GEN-01 | 生成文件头包含 schema hash 与 `DO NOT EDIT` | `head -5` |
| GEN-02 | 同输入两次生成字节一致 | `nexcore-gen --check` + golden 测试 |
| GEN-03 | 生成 C 不含函数式宏、`##`、`__VA_ARGS__`；只允许对象式常量宏与 include guard | `grep -nE "#define[[:space:]]+[A-Za-z_]+\\("` 必须为空 |
| GEN-04 | 生成代码不含 `dlsym`、`Mirror`、`kotlin-reflect`、`Proxy`、`eval`、`new Function` | `grep` 黑名单 |
| GEN-05 | 每个生成函数体 ≤ 40 行；每个 C 包装函数只调用一个 Core handler | 行数用脚本；“只调用一个 handler”用生成器内部 AST 断言（生成器本来就有 AST，不用正则） |
| GEN-06 | 每个公开标识符可回溯到 Schema 路径；命名符合 §7 | 生成器单测 |
| GEN-07 | 公开 API 不含 `Any`/`dynamic`/`JSONSerialization`/`JsonElement` | `grep` |
| GEN-08 | 方法名字符串只出现在注册表与生成绑定中，不出现在业务代码 | `grep -r` 业务目录 |
| GEN-09 | 生成物不含时间戳、绝对路径、主机名、随机顺序 | 生成器不读环境；diff 稳定 |
| GEN-10 | 生成器不联网、不读环境变量、不写输出目录之外 | **人工规则**：评审签字 + CI 沙箱记录；不假装成自动检查 |
| GEN-11 | 所有生成物在同一提交内更新 | CI `--check` |
| GEN-12 | 每个方法在 C/Swift/Kotlin/JS 四端各声明一次，且至少有一个调用点 | 生成器计数断言（注册表字符串不计入声明） |
| GEN-13 | 标识符冲突是错误，不自动重命名 | 负例测试 |
| GEN-14 | 生成代码不吞错误：每个 `catch` 必须转成错误码或重新抛出 | **人工规则**：模板评审签字；自动侧只能检查“存在 catch 分支” |
| GEN-15 | 生成 C 必须通过 `-Wall -Wextra -Werror` | CI 编译 |
| GEN-16 | **禁止跨单元取函数地址**：不生成 `&<qualified>`、`= <qualified>`、`(&f)`（`f` 来自 import）；需要回调时在调用方生成 `static` wrapper，再 `&wrapper`。wrapper 内的调用必须落到本单元可见的符号（同单元定义或生成器注入的 `extern` cname） | 不做正则扫描；生成物直接过 `cc -Werror=implicit-function-declaration -fsyntax-only`，符号不可见即失败 |
| GEN-17 | **Bridge 单元拓扑**（对应 FIX-16）：为每个目标命名空间生成 `<ns>/<ns>_map.m.ct`（路径必须与 namespace 目录一致）；消费者生成 `import <ns>::<ns>_map;`；跨单元调用一律生成 `<ns>::<Trait>.<method>`，不得生成裸名或手写 cname。实现单元**必须**有 `import <self>;`；其余 import 只允许 `<ns>::<ns>_map`；禁止其他实现单元（含 `<ns>::<ns>` 这种头单元）。映射单元内部同样只允许自己，不得 import 实现单元 | 单元清单断言 + SYM-05 检查；负例（无映射单元的裸名/限定名/cname、跨实现单元 import、实现单元缺 self import、map 内部非法 import）必须失败 |
| GEN-18 | 生成器不得假设 `ns::f` 能作为值；函数指针注册一律走 wrapper | 与 GEN-16 共用编译门禁；`grep -nE '(&|=)[[:space:]]*[A-Za-z_][A-Za-z0-9_]*::' schema/generated/c` 必须为空 |
| GEN-19 | 生成的 C 不输出 CT 限定名：调用点经映射单元落到 cname（`s_k`）；cname 由符号表提供，不由生成器拼字符串 | 剥注释/字符串后 `grep -nE '::' schema/generated/c` 必须为空；`cc -fsyntax-only` 通过 |
| GEN-20 | **映射单元生成**：`<ns>/<ns>_map.m.ct` 只含 prologue include、静态 trait、cname recipe，不含实现；recipe key 固定为 `(ns, trait, method)`；recipe 的 callee 必须等于符号表（`src/units.json`）里的 cname；映射单元本身不 import 实现单元。附加约束：实现单元禁止 `_map` 后缀；同一 `(ns, trait, method)` 重复即失败；两个 trait 同名方法落到同一 cname 但签名不同即失败；prologue include 用带目录的相对路径，头文件 basename 冲突即失败 | 生成物 golden + 三处一致性断言（目标导出头/recipe/调用点）+ 重复 key/签名冲突/头名冲突负例；`cc -fsyntax-only` |

这些规则的目的：让评审者可以只读生成物就判断“它做了什么”，不需要相信生成器的隐式行为；也让 AI 生成的代码无法藏匿调用链。

规则分两类，验收时不得混淆：**自动规则**（GEN-01..GEN-09、GEN-11..GEN-13、GEN-15..GEN-20、SYM-01..SYM-05）由脚本或编译器判定；**人工规则**（GEN-10、GEN-14）只做评审签字，不冒充自动门禁。

### 8.1 符号解析规则（SYM 系列）

实测事实（`/tmp/ctmap`，A、B 均为命名空间单元）：

| 场景 | CT | 生成 C | `cc` |
|---|---|---|---|
| 无映射：裸名 `f(v)` | exit 0，零诊断 | `f(v)`，不生成 `#include` | `call to undeclared function 'f'` |
| 无映射：限定名 `s::f(v)` | exit 1 | — | — |
| 无映射：手写 cname `s_f(v)` | exit 0 | `s_f(v)`，无声明 | `call to undeclared function 's_f'` |
| 有映射：`b::X.call(x)` | exit 0 | `(b_bfn((x)))` + `#include "b.h"` | 通过 |
| 有映射：反向 `a::Y.call(x)` | exit 0 | `(a_abase((x)))` + `#include "a.h"` | 通过 |

结论：跨单元调用不是“换一种写法”，而是“必须存在映射单元”；映射单元由生成器按符号表产出。

| ID | 规则 | 检查 |
|---|---|---|
| SYM-01 | **符号表先行**：P3 的生成器输入包含全部单元、`namespace`、`import`、函数/变量定义、可见性与 cname（当前阶段 `src/units.json` 只声明需要映射的 `(namespace, trait, methods)`，cname 由规则产出并用 `--verify-ct` 校验）；发射前必须查到目标符号。符号键为 `(ns, trait, method)`，trait 为空时退化为 `(ns, "", method)`；重复 key 或同一 cname 对应不兼容签名即失败。查不到即失败，不得输出任何猜测名字 | 生成器单测：缺符号、重复 cname、重复 key、签名冲突、跨单元目标各一例；失败必须发生在生成阶段 |
| SYM-02 | **生成 C 只用 cname**：跨单元调用经映射单元落到 cname；C 文件中不允许出现 `::`（剥注释/字符串后判定），也不允许依赖调用方单元手写 include | 剥注释/字符串后 `grep -nE '::' schema/generated/c` 为空 + `cc -fsyntax-only` |
| SYM-03 | **取址只指向本单元**：`&name`/`= name` 只能是本单元定义的函数或本单元生成的 wrapper；跨单元函数一律先生成 `static` wrapper，再取 wrapper 地址（GEN-16/GEN-18） | 编译门禁 `cc -Werror=implicit-function-declaration -fsyntax-only`；不看 CT 退出码 |
| SYM-04 | **生成 CT 源码的跨单元调用只允许经映射单元**：实现单元**必须** `import <self_base>`（单段 base，自身 `.h.ct`；漏掉时 CT 静默 `exit 0` 但会丢命名空间前缀），并可 `import <ns>::<ns>_map`；禁止其他实现单元（含 `<ns>::<ns>`）。映射单元**不得 import 任何实现单元**（含自己的 `<ns>`），唯一允许 `<ns>::<ns>_map`；不得生成裸名、手写 cname 或根转发调用 | 生成器结构断言 + 脚本负例（无映射的三种写法、跨实现单元 import、实现单元缺 self import、map 内部单段 import） |
| SYM-05 | **五条机械检查**：① `ct build` 退出码 0；② 每个生成的 `.c` 过 `cc -Werror=implicit-function-declaration`；③ 剥掉注释与字符串字面量后生成 C 中无 `::`；④ recipe 的 callee、目标导出头中的原型、调用点的 `trait.method` 三者与符号表一致（生成器内部 AST 断言 + golden 比对，不用外部正则冒充）；⑤ import 白名单分两条——**a) 每个实现 `.ct` 必须以 `import <self>;` 开头**（缺了 CT 不报错，必须显式拦）；**b) 只允许自身 `.h.ct` 与 `*_map.m.ct`，且 `*_map.m.ct` 不整类跳过**，按同一白名单检查其内部 import（映射单元没有自身 `.h.ct`，因此任何单段 import 都违规）。脚本按解析后的绝对路径判定，不按字符串后缀 | CI 脚本逐条输出；⑤a/⑤b 各有负例；④ 由生成器 `--verify-ct build/impl` 输出 recipe 与导出符号的 diff；任一失败即拒绝。⑤a/⑤b 已随 `check.sh` 全量实跑通过（`SCRIPT_RC=0` + 逐项 PASS），负例均确认真失败；另有 ⑥ stat key 契约（`tests/stat_keys.c`）同为实跑通过 |

补充边界（实测）：该问题**只影响函数，不影响变量**。`&g_var` 会正确生成 `&a_b_mod_g_var`，因此不为变量增加同样的防护。

> 原 R-01/R-02/R-03 已删除并说明：R-01/R-02 建立在“没有映射单元也能跨单元调用”的旧前提上，现由 SYM-01..SYM-05 取代；R-03 用正则扫描 C 符号不可靠（宏、typedef、局部变量都会误报），由编译器前端取代。

## 9. 变更传导

### 9.1 新增方法（增量）

1. 改 Schema（新增 method/event/optional 字段）。
2. 运行 `nexcore-gen`，提交 4 个生成物 + manifest。
3. 实现 Core 的 `c_handler`（若 `side: core`）或 UI/JS handler。
4. 加一条协议测试（请求/响应/错误）。
5. CI：schema 校验、生成物无漂移、C 编译、三端单测。

不需要改 Bridge；如果改了，说明抽象漏了，评审要追问。

### 9.2 破坏性变更

- 删除/重命名方法、改参数/结果类型、改 `side`、改事件 `delivery` 语义。
- 必须升 `protocol_version`，三端同版本发布；生成器在 `--allow-breaking` 下才允许，并在 manifest 标记。
- 旧版本 App 遇到新 schema hash → `nexcore_create` 返回 `NX_E_SCHEMA_MISMATCH`，明确失败，不尝试兼容。

### 9.3 运行时校验

- `struct NexCoreConfig` 的 `schema_hash` 与 C 注册表、JS bootstrap 的常量三方比对。
- 不匹配：`nexcore_create` 返回 `NX_E_SCHEMA_MISMATCH`；JS 侧 bootstrap 抛错。
- 生产环境不允许跳过校验；debug 可用全零 hash 跳过，但必须打日志。

## 10. 构建链路（与 CT 导出方式绑定）

只有 `ct build --emit-c` 生成完整头；`ct compile` 的头是空壳且不接受 `import`。因此生成物与 CT 产物的固定链路是**两趟构建**（发布头与实现分开）。**两趟命令与 `build/abi` / `build/impl` 的路径口径只在 design.md §6.1 定义一次**，此处不重复，只列本链路特有的后续步骤：

```bash
# 前提：已按 design.md §6.1 完成 Pass 1（build/abi）与 Pass 2（build/impl）

# 1) 生成器：当前阶段只生成/校验映射单元（写回 src/）；CI 用 --check 做漂移检查
python3 tools/nexcore_gen.py --spec src/units.json --src src
python3 tools/nexcore_gen.py --spec src/units.json --src src --check
#    P3 接入 Schema 后再追加：--schema schema/nexcore.schema.yaml --out schema/generated

# 2) 严格编译：所有 Bridge 单元的 C + 生成器 C
#    -I 首位必须是 build/abi；build/impl 下的 NexCore.h 是实现中间物，不发布
cc -std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration \
   -Werror=incompatible-pointer-types -fPIC \
   -I build/abi -I build/impl -I schema/generated/c \
   -I src/js_host -I src/queue -I src/protocol \
   -fsyntax-only build/impl/*.c schema/generated/c/*.c
cc -std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration \
   -Werror=incompatible-pointer-types -fPIC \
   -I build/abi -I build/impl -I schema/generated/c \
   -I src/js_host -I src/queue -I src/protocol \
   -c build/impl/*.c schema/generated/c/*.c

# 2b) 断言实现单元真的发射了函数体（不是只有同名原型；macOS nm 带前导下划线）
nm -g build/impl/*.o | grep -Eq ' T _?nexcore_create$'

# 3) 布局 golden：编译并运行一个小程序，打印每个 ABI 类型的 size/offsetof
cc -I build/abi tools/abi_layout_dump.c -o build/abi/abi_layout_dump
build/abi/abi_layout_dump > build/abi/abi-layout.txt
diff -u abi/abi-layout.txt build/abi/abi-layout.txt

# 4) 链接
cc -shared ... build/impl/*.o schema/generated/c/*.o \
   -lquickjs -lm -lpthread -o build/libnexcore.so
```

**构建链的三个已知风险**（与 design.md §5.5 同一口径）：

1. **`.h.ct` 与 `.ct` 不自动配对**：只列 `.h.ct` 时，同目录实现不会进来，链接缺符号；两者都进构建时都写 `<out>/<base>.c`。实测**两种输入顺序产物 `diff` 为空**，但这一致性没有文档保证。固定用“Pass 2 只列实现 `.ct` + `nm` 断言符号为 `T`”，不依赖覆盖语义。
2. **shim 必须挂在映射单元上**：`<base>.shim.h` 只能与映射单元同目录同 base（如 `src/queue/queue_map.shim.h`），不能放一个独立的 `shim/` 目录；编译时把 `src/queue`、`src/js_host` 加入 `-I`。
3. **发布头来源唯一**：任何平台或测试都只能 `-I build/abi`。误用 `build/impl/NexCore.h` 会把内部 struct 前置声明重新带回消费侧，属于静默污染。

平台侧：

- Swift：`swiftc -import-objc-header build/abi/NexCore.h`（已实测）；SPM 用 system library target + modulemap。
- Kotlin/JVM + Android：JNI 包装只 include `build/abi/NexCore.h`；`kotlinc` 已验证，Kotlin/Native 未验证。
- 任何平台都不得手写第二份 `struct NexCoreConfig` 或 ABI 结构体；必须用 CT 导出的头。

**typedef 真实性**：CT 不检查 typedef 别名（`typedef char* MyInt;` 可被当作 `int` 使用）。因此 ABI 类型不得使用别名表达标量；生成器只生成 C，由 `-Werror=incompatible-pointer-types` 与布局 golden 兜底。

生成器不参与最终链接；它只产出源码。生成物中的 `nx_core_*` wrapper 由 C 编译器编译，`nexcore_registry.c` 与 `NexCore.c` 同属一个链接单元。

## 11. 反例

以下写法即使“能跑”也不接受：

```c
/* 反例 1：宏生成调用链，评审看不到每个方法实际做了什么 */
#define NX_METHOD(name, fn) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, 0));

/* 反例 2：用反射/字符串拼接找方法 */
JSValue fn = JS_GetPropertyStr(ctx, global, method_name);

/* 反例 3：把整个对象 JSON 再解析成 C，绕过显式字段映射 */
char *json = JS_JSONStringify(ctx, argv[0]);
my_custom_parse(json, &out);

/* 反例 4：Swift 公开 API 用字典传递参数 */
public func call(_ method: String, _ params: [String: Any]) async throws -> Any

/* 反例 5：Kotlin 用 Gson 反射序列化，字段改名后运行时才炸 */
data class CartLine(val sku: Int, val qty: Int)
```

正确做法是：每个方法一个可见函数、每个字段一行显式读写、每个平台一个显式类型。
