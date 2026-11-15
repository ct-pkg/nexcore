# 反证席评审：Shell 宿主「阶段 0」（PH-10）

> 日期：2026-11-15 ｜ 立场：**反证席**（目标是找出返工点、ABI 债、门禁假绿；攻不倒的明写「站得住」）
> 基线：`HEAD = 799a4e3`（评审期间仓库从 `249b178` 前进到 `799a4e3`：T1–T4 已落地）。**本文行号一律以 `799a4e3` 为准。**
> 在途：`tools/check.sh` 工作树有 60 行未提交改动（另一会话的 ⑰），本文**不引用该改动、不跑全量门禁**；所有实测在 `/tmp` 独立副本完成。
> 基线校准：提交本文时仓库已前进到 `aaa8428`（T5 `19b7934` 产出 `.a`、T6 `aaa8428` 加 `tests/hosts_boundary_scan.sh`）。§4/§5 评的是**卡面判据文本**，实现落地不改变这些判据的可执行性问题；§5.2 已补 T6 实际用法的说明。
> 对象：[裁定](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md)、[PH-10 卡](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md)、[修订清单](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-proposal-revisions.md)；并对照同轮[主张席评审](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-stage0-proponents-review.md)。
> 纪律：只读仓库 + 只写本文件；未改 `src/`、`tools/`、卡片、`abi/`；未 push、未打 tag。

---

## 0. 结论表

| # | 攻击点 | 判定 | 一句话 |
| --- | --- | --- | --- |
| A1 | 「复用尾部 padding」 | **不成立** | 三份文档无此原文（grep 0 命中）；实测 `padding_after_log_user=0`，88→120 是纯追加 |
| A2 | 卡 §4.2 第 1 条阈值 | **不成立（内存安全）** | 按字面实现，`struct_size=88` 的旧调用方会被读 88..95，ASan 实测堆越界 |
| A3 | 落地阈值 `>= CURRENT_SIZE` | **今天对、以后错** | 下一次尾追加后，所有 120 字节旧宿主静默失去 Provider（ABI 债） |
| A4 | E3「老调用方零影响」夹具 | **假绿** | 夹具用 120 字节分配冒充 72 字节旧调用方，物理上不可能观测越界；88 这一档完全没测 |
| A5 | 名字白名单 | **名不副实** | `__nx_native` 常驻全局且不校验名字，脚本可调任意未挂载名；白名单只是挂载糖 |
| A6 | 名字/参数生命周期 | **规范缺失** | 头里没写；实现把宿主指针存进 state（`quickjs:141`），只靠「今天只读一次」侥幸成立 |
| A7 | 泵复用 `nexcore_drain` | **短期成立、长期欠账** | drain 与 shutdown 都不泵；宿主没有「不发消息推进一个 turn」的入口 |
| A8 | E1 覆盖 post-pump | **不成立** | 与主张席 §3.4 一致：A/B 两例靠 pre-pump 就能过 |
| A9 | `max_jobs` | **两头都错** | `<=0` 无界（自续链永久挂死）；`>0` 早退且 shutdown 不补泵 → 丢任务；两条都无夹具 |
| A10 | E5④「只链 `.a` 跑出 SMOKE OK」 | **两档都不可执行** | null 档实跑 `run_rc=6`；quickjs 档必须再链 `libquickjs.a` |
| A11 | `.a` 与「7 个导出」 | **可绕过** | 实测归档露出 **19 个** `T` 符号，含 `_js_backend_pump`；宿主可直接调内部 |
| A12 | 「只链 `.a`」的命令写法 | **可假绿** | 实测 `-lnexcore` 静默选中 `.dylib`（`otool -L` 为证） |
| A13 | E5⑤ 负例 | **判据太弱** | 只判 `rc≠0`；实测 `-undefined,dynamic_lookup` 能让缺 `NexCore.o` 的归档照样产出可执行 |
| A14 | E5① 对象集合 | **字面实现恒红** | macOS `ar t` 首行是 `__.SYMDEF SORTED`，与 `*.c` 派生集合永不相等 |
| A15 | 卡 §4.3 对 quickjs 对象数的描述 | **事实错误** | quickjs 档只编 `js_backend_quickjs.ct`，不存在 `js_backend.o` |
| A16 | `hosts/` 纳入扫描 | **扫了但不阻断真实交付物** | 只 `find -name '*.ct'`；C 宿主零覆盖，T6 的 `scanned` 增长与 E7 互斥 |
| A17 | 四件最小集 | **缺三件、多两字段** | 缺异常/拒绝可见性、`JS_JOBS`/`JS_EXCEPTIONS` 采样、shutdown 泵；`native_names(+len)` 可减 |

---

## 1. 基线与方法

| 项 | 值 |
| --- | --- |
| 基线 | `799a4e3`（`git status` 除一个无关未跟踪目录外干净） |
| 工具链 | `~/.local/ct/0.44.2/ct`（`ct v0.44.2`） |
| QuickJS | **本机没有源码**（`experiments/third_party/quickjs` 不存在），故所有 quickjs 档结论是「代码路径 + 行号」，不是运行输出 |
| 实测手段 | 用 `ct build --emit-c` 生成发布头到 `/tmp`，再用 `tools/abi_layout.py` 与 `cc` 探针测量；null 档在 `/tmp` 独立构建归档 |

> 为什么不用主张席的附录 A2：那份把 4 个字段「套在 struct 外面」测偏移，结论对，但它只测了偏移，**没有测 88 这一档的读取安全**（见 A2/A4）。本文补的是「读」的实测。

---

## 2. 攻点一：Provider 塞进 `NexCoreConfig` 尾部

### 2.1 「复用尾部 padding」——三份文档里没有这句话，且事实上没有 padding

```
$ grep -rn "padding" docs/reviews/nexcore-shell-host-ruling.md docs/phases/PH-10/card.md \
    docs/reviews/nexcore-shell-host-proposal-revisions.md
(无输出)
```

真实布局（`tools/abi_layout.py` 用 `cc` 实测 `build/abi/NexCore.h`）：

```
NEXCORE_CONFIG_MIN_SIZE = 72
NEXCORE_CONFIG_CURRENT_SIZE = 88
[struct NexCoreConfig] size=88 align=8
  log       offset=72 size=8
  log_user  offset=80 size=8
```

追加 4 个 8 字节标量后：

```
NEXCORE_CONFIG_MIN_SIZE = 72
NEXCORE_CONFIG_CURRENT_SIZE = 120
native_call=88 native_user=96 native_names=104 native_names_len=112
log_user_end=88 padding_after_log_user=0
```

结论：`log_user` 的结束偏移正好是 88，`88 % 8 == 0`，**尾部 padding 为 0 字节**。所以：

- 任何「复用尾部 padding」的说法**前提错误**——没有东西可复用；120 是 88+32 的纯追加。
- 主张席 §4.3 第 3 点写「尾块无填充（88→120 严格递增 8 字节）」是**对的**，但它论证的是「直接追加 == 外面套一层」，不是「零影响」。两件事别混。

### 2.2 卡面阈值按字面实现 = 88 字节旧调用方的堆越界（A2）

[card.md:98](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L98) 原文：

> `struct_size` 小于 `offsetof(native_call)` 的调用方**视为未提供**，库不得读越界

`offsetof(native_call) = 88`。于是 `struct_size == 88` 不满足「小于」，库**应当读** 88..95——而 88 正是今天所有宿主按旧头 `sizeof(cfg)` 传进来的值（改头之前的 `CURRENT_SIZE`）。按该条写成探针（`-I` 指向 `799a4e3` 重新生成的发布头，不是手抄布局），ASan 结果：

```
MIN=72 CURRENT=120 sizeof=120 offsetof(native_call)=88 offsetof(native_names_len)=112
=================================================================
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 8
0x6080000049f8 is located 0 bytes after 88-byte region [0x6080000049a0,0x6080000049f8)
```

这不是「保守/激进」的口径差异，是**读越界**。主张席 §4.7 把这条降级为「对 96/104/112 判定不同」，漏掉了 88 这一档；而 88 是**唯一真实存在的旧调用方尺寸**（96/104/112 从来不存在）。

### 2.3 落地阈值 `>= CURRENT_SIZE`：今天对，下一次尾追加就静默失效（A3）

落地代码（[NexCore.ct:89](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L89)）：

```c
if (cfg->struct_size >= NEXCORE_CONFIG_CURRENT_SIZE) {
    native_call = cfg->native_call; ...
}
```

`NEXCORE_CONFIG_CURRENT_SIZE` 是**编译期**常量 `sizeof(struct NexCoreConfig)`。这解决了 88 的越界，但把「是否提供」绑死在「库自己有多大」：

- 将来任何一次尾部追加（`CURRENT_SIZE → 152`），所有按 120 字节头编译的宿主都会走「未提供」分支；
- 它们不会崩、不会报错，只是 `Shell` 不存在——与段错误的区别只是更难查。

正确判据是按字段组判：`struct_size >= offsetof(native_call) + sizeof(native_call)`。四个字段同宽，一次判 `>= 120` 也行，但**不能用会漂移的 `CURRENT_SIZE`**。这条同时是「卡面写错了、实现换了种写法、两边都没写对」的三重偏差。

### 2.4 E3「老调用方零影响」夹具是假绿（A4）

[Card.md:144](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L144) 要求：`struct_size = NEXCORE_CONFIG_MIN_SIZE` 的老调用方 → `create` rc=0 且无 `Shell` 全局。夹具 [native_provider.c:174-183](file:///Users/samxx/trae-ws/nexcore/tests/native_provider.c#L174-L183)：

```c
unsigned char *raw = (unsigned char *)malloc(sizeof(struct NexCoreConfig));  /* =120 */
memset(raw, 0xA5, sizeof(struct NexCoreConfig));
old->struct_size = (uint32_t)NEXCORE_CONFIG_MIN_SIZE;                        /* 声明 72 */
memset(raw + NEXCORE_CONFIG_MIN_SIZE, 0xA5, sizeof(...) - MIN);
```

它分配的是 **120 字节**，只是把 `struct_size` 声明成 72。所以任何「读到 88..112」的越界都落在自己的合法堆块内，ASan 不会报；断言 `typeof Shell === "undefined"` 也区分不了——即使真读了 0xA5A5…，`native_names_len` 被强转成负数后 `install_natives` 同样直接返回 0（[js_backend_quickjs.ct:70](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L70)），`Shell` 仍然是 `undefined`。

更糟的是门禁文案把没做到的事写成了做到：[check.sh:641](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L641) 的失败信息是「原生 Provider 未生效**或越界读了尾字段**」。这个夹具永远抓不到越界读。

**修法**：老调用方必须**真的只分配 72 字节**（`malloc(MIN)`），并在 ASan 下跑；同时补 88 这一档（今天真实存在的旧尺寸）。

### 2.5 白名单不是访问控制（A5）

挂载脚本把名字挂到 `globalThis`，然后**不删 `__nx_native`**（[js_backend_quickjs.ct:38](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L38) 注释明说），而回调入口不校验名字：

```c
/* js_backend_quickjs.ct:103-114 */
if (st == 0 || st->native_call == 0) return JS_ThrowTypeError(...);
if (argc < 2) return JS_ThrowTypeError(...);
name = JS_ToCStringLen(ctx, &name_len, argv[0]);
...
rc = st->native_call(st->native_user, name, name_len, args, args_len, &out, &out_len);
```

也就是说脚本里 `__nx_native("Shell.execSync", '["rm -rf /"]')` 可以调到宿主**没有**放进 `native_names` 的名字。白名单只决定「自动挂哪些便利函数」，不决定「能调哪些能力」。卡面 D2 写「单回调 + 名字白名单」（[card.md:52](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L52)），读起来像一道权限边界；实际不是。

要么在 `js_native_call` 里对 `name` 做白名单比对（成本：一次线性查找），要么把这条能力从「白名单」改名为「挂载清单」，并写清「权限由宿主 `native_call` 自己判」。

另外 `__nx_native_names` 也留在全局（[:75](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L75)），脚本可读到完整清单。

### 2.6 名字/参数的生命周期（A6）

三根字符串，卡里只写了第三根：

| 串 | 谁持有 | 有效期 | 卡面是否规定 |
| --- | --- | --- | --- |
| `native_names`（白名单 JSON） | 宿主 | 实现只在 `create` 内读（`install_natives`），但**指针被存进 state**（[:141](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L141)） | **没写** |
| `name` / `args_json`（传给 `native_call`） | 库临时缓冲（`JS_ToCStringLen`） | 仅回调期间；宿主必须自己拷贝 | **没写** |
| `out_json` | 宿主 `malloc`，库 `free` | 见 [:122](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L122) | 写了（[card.md:101](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L101)） |

还有一处语义缺口：卡面只说「名字按 `.` 分段挂到全局」，**没说回调收到的 `name` 是全名还是末段**。夹具按全名写（[native_provider.c:30](file:///Users/samxx/trae-ws/nexcore/tests/native_provider.c#L30) 注释「不是末段」），宿主若按卡面猜末段就会全挂。

### 2.7 32 位 / 不同对齐（A 部分不成立，但没法证明）

- 本机 `cc -m32` 不可用（`missing required architecture armv4t` / `tapi error`），**无法实测**。
- 可以确定的是：`NEXCORE_CONFIG_MIN_SIZE` 本身随 ABI 变（32 位下 `size_t/指针` 4 字节，MIN 不会是 72），而 `abi/abi-layout.txt` 只有**一份**、由本机 `cc` 生成。所以 ⑪ 在非 LP64 目标上不是「假绿」而是「假红」。
- 这不算 PH-10 的缺陷（仓库只发 macOS 产物），但卡面 I-3「老 `struct_size` 调用方行为不变」必须加主语：**在同一 ABI/同一头库对下不变**。

### 2.8 此处站得住

- 尾部追加确实**不改** `NEXCORE_CONFIG_MIN_SIZE`（仍是 72，实测）、不改 `NEXCORE_ABI_VERSION`（仍 1）、不改 7 个导出函数；在 `I-1/I-2` 的约束下，这是唯一不撞冻结面的落法——主张席 §4.2 的排除法成立。
- 4 个标量而不是 vtable/新 struct，正确：`①b` 只允许 `NexCoreConfig` 一个 struct（[check.sh:217](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L217)），vtable 会被 `①b/①c` 当场红。
- `native_names` 用「JSON 串 + len」而不是 `const char **`，在 ABI 面确实更省（不需要在头里表达数组元素生命周期）——只是这 2 个字段的**收益**被高估了（见 §6.3）。

---

## 3. 攻点二：泵复用 `nexcore_drain`

### 3.1 「空 drain 三轮不跑 Promise」说明什么

`nexcore_drain` 的实现（[NexCore.ct:246-262](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L246-L262)）：

```c
(void)max_wait_ms;
pump_inbox(impl);
deliver_outbox(impl, out_count);
return NX_OK;
```

**没有泵**。泵只有两处，都在 `Backend.eval` 内（[quickjs:192](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L192) 前置、[:239](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L239) 后置）。所以：

- 裁定 §2.1 那个 `drain ×3 → payload=0` 的实测，在 T1 之后**依然复现**（只是现在要有一个 eval 才会动）；
- 它说明的不是「泵没做」，而是**「turn 的推进权在 eval，不在宿主」**。宿主无法在不发消息的情况下把 job 队列推进一步；
- 这条约束今天无害（PH-10 没有异步生产者），但 PH-12 的 `Shell.spawn` Promise 化、任何 native 异步回调，都需要「没有新消息也要泵」的入口。届时只有两条路：改 `drain` 的语义（所有宿主行为一起变）或新增 ABI（红 ⑩）。这就是「复用 drain」欠下的账。

### 3.2 E1 只证明 pre-pump（同意主张席）

A/B 两例的 job 都是 bootstrap 排的（[pump_microtask.c:61-64](file:///Users/samxx/trae-ws/nexcore/tests/pump_microtask.c#L61-L64)、[:92-95](file:///Users/samxx/trae-ws/nexcore/tests/pump_microtask.c#L92-L95)），第一条消息进入 eval 时先跑 pre-pump 就清空了；`:239` 删掉，⑮ 仍 PASS。主张席 §3.4 已指出，我复核后**同意**，并补一条他们没说的：

**E1 的「两档都要 PASS」（[card.md:142](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L142)）在 null 档是空话**——⑮ 在 null 档输出 `N/A`（[check.sh:631](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L631)），永远不可能 PASS。E1 作为**退出判据**按字面无法满足。

### 3.3 `max_jobs`：定错是死循环，也是丢任务（A9）

实现（[quickjs:270-283](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L270-L283)）：

```c
limit = max_jobs;
if (limit <= 0) limit = 0x7fffffff;
while (ran < limit) { rc = JS_ExecutePendingJob(...); ... }
```

- `max_jobs <= 0`（当前唯一用法）：**无界**。`var f=function(){Promise.resolve().then(f)}; f();` 这种自续链永远返回不到 0，第一条 `send+drain` 就永久挂死。卡面明确不做 D-05 预算（[card.md:75](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L75)），所以这是「已知且接受」；但接受的是「本地工具能被自己的脚本挂死」，必须写进 PH-11 的安全口径，不能默认读者知道。
- `max_jobs > 0`：早退。剩余 job 只能等下一次 eval 的 pre-pump；而 `shutdown` 不泵（[NexCore.ct:275-280](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L275-L280) 只 `pump_inbox`，inbox 空就不 eval），所以**最后一条消息排下的 job 会被静默丢弃**。
- 两条路径都没有夹具：全仓 `pump(h, 0)` 两处，`pump(h, 正数)` 零处。

### 3.4 异常与拒绝被吞掉

`pump` 遇到 job 异常时 `JS_GetException` → `JS_FreeValue` → `return -1`（[:276-279](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L276-L279)），而 eval 的调用点是 `(void)pump(h, 0);`（[:192](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L192)、[:239](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L239)）。于是：

```js
Promise.resolve().then(function(){ throw new Error("boom") });
```

eval 返回 `NX_OK`，sink 收到表达式的值，`last_error` 仍是 0。**PH-10 新增了一条「脚本错了但全绿」的路径**，而错误面按卡面排在 PH-11。D1 自己引入的静默失败，应该由 D1 处理（至少计数），不能记在下一阶段账上。

### 3.5 此处站得住

- 「泵放在 eval 内、不新增导出符号」在 `I-1` 下确实是唯一合法路径；主张席 §3.1/§3.2 的三处断言（7 个 `T`、`nexcore_*` 前缀、白名单 7 行）我复核无误。
- 泵的 `JSValue` 返回值在官方示例里应 `JS_FreeValue`——当前实现用 `JS_ExecutePendingJob` 的返回值只判 `>0/0/<0`，没有单独取 `JSValue`，**这里没有泄漏**（`JS_ExecutePendingJob` 的 result 是出参，不是返回值）。这条主张席没说，我确认**站得住**。

---

## 4. 攻点三：`.a` 判据

### 4.1 E5④ 两档都不可能（A10）

E5 第 ④ 条（[card.md:146](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L146)）：「`tests/smoke.c` 只链 `.a` 能跑出 `SMOKE OK`」。

我在 `/tmp` 用 null 档对象手工组了归档，实测：

```
$ cc -std=c99 -Wall -Wextra -Werror -I /tmp/nxbuild/abi tests/smoke.c libnexcore.a -o smoke_a
$ ./smoke_a
...
drain rc=0 count=0 sink.count=0
run_rc=6
```

链接成功、运行到 `drain` 就返回 6，**没有 `SMOKE OK`**——因为 null 档 `eval` 恒返回 -1，什么也投递不出。

quickjs 档更直接：`libnexcore.a` 只归档 `build/impl/obj/*.o`，**不包含引擎**（对照 dylib 的链接命令 [check.sh:454-456](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L454-L456) 是把 `$QJS_LIB` 一起链进 dylib 的）。所以 `js_backend_quickjs.o` 里的 `_JS_NewRuntime` 等符号在「只链 `.a`」时无定义，链接必失败。

E5④ 实际上隐含「`.a` + `libquickjs.a` + `-lm -lpthread`」，与「只链 `.a`」互斥。**卡面要么写成「只链 `libnexcore.a`（null 档可链接）+ quickjs 档另备引擎」，要么拆成两条判据。**

### 4.2 `.a` 不是「只有 7 个导出」，而且把内部能力重新打开了（A11）

实测同一批对象：

```
$ nm -g --defined-only libnexcore.a | awk '$2=="T"{print $3}' | sort
_js_backend_eval _js_backend_init _js_backend_pump _js_backend_shutdown _js_backend_turns
_nexcore_create _nexcore_drain _nexcore_free _nexcore_send _nexcore_set_sink
_nexcore_shutdown _nexcore_stat _protocol_request_well_formed _protocol_response_kind
_queue_close _queue_depth _queue_open _queue_pop _queue_push
（19 个）

$ nm -gU libnexcore.dylib | awk '$2=="T"{print $3}' | sort
_nexcore_create ... _nexcore_stat（7 个）
```

`-exported_symbols_list` 只对 dylib 生效，**对静态归档无效**。后果两条：

1. I-1「导出 ABI 函数仍是 7 个」在 `.a` 交付形态下**不成立**；E5② 只验「含 7 个」，不验「只 7 个」，所以门禁绿着。
2. 裁定 §5 选 C 宿主的核心理由是「C 宿主拿到的只有 7 个函数，天然无法绕过 ABI」。`.a` 把这个前提拆了：`_js_backend_pump`、`_queue_push` 都是可链接的 `T`，宿主直接 `extern` 就能调。既然交付 `.a` 给宿主，就应显式规定「宿主只许用 7 个」并加一条 `nm` 负例，否则这条纪律只是口头约定。

顺带：`_js_backend_turns` 在归档里也露着——见 §6.2。

### 4.3 `-lnexcore` 会静默选中 dylib（A12）

在 `/tmp` 同时放 `.a` 与 `.dylib` 后：

```
$ cc ... tests/smoke.c -L/tmp/nxbuild -lnexcore -o smoke_lib && otool -L smoke_lib
smoke_lib:
	libnexcore.dylib (compatibility version 0.0.0, current version 0.0.0)
```

macOS 的链接器在 `-l` 下优先 `.dylib`。所以「只链 `.a`」必须写成**归档的显式路径**（`... libnexcore.a`），并加一条 `otool -L` 断言「无 `libnexcore.dylib` 依赖」。写 `-lnexcore` 的夹具会在 `.a` 根本不存在时也照样绿。

### 4.4 E5⑤ 只判 `rc≠0`，可被链接选项绕过（A13）

实测：`ar d NexCore.o` 后正常链接失败，且错误文本点名符号：

```
Undefined symbols for architecture arm64:
  "_nexcore_create", referenced from:
```

这条**站得住**（`ar d` 删不存在的成员也会 `rc=1`：`ar: nope.o: not found in archive`，不会静默空转）。但判据只写「链接必须失败（rc≠0）」：

```
$ cc ... -Wl,-undefined,dynamic_lookup tests/smoke.c d2.a -o smoke_dyn
built smoke_dyn
```

`-undefined dynamic_lookup` 会让缺 `NexCore.o` 的归档**照样产出可执行**（运行时才崩）。只要将来的 `check-host.sh` 或 `.a` 消费方带了这类 flag，E5⑤ 就空转。修法：负例必须断言 stderr 里出现 `_nexcore_create`，并显式禁 `dynamic_lookup`。

### 4.5 `ar t` 含 `__.SYMDEF`（A14）

```
$ ar t libnexcore.a
__.SYMDEF SORTED
js_backend.o
NexCore.o
protocol.o
queue.o
```

E5① 写「对象集合 == `basename(build/impl/*.c)` 集合」，字面实现会永远不等（多一条 `__.SYMDEF`）。必须写「只取 `*.o` 成员」或排除 `__.SYMDEF`。这是**恒红**而不是假绿，但同样让判据不可执行。

### 4.6 卡面 §4.3 的 quickjs 对象描述是错的（A15）

[card.md:110](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L110)：

> quickjs 档会同时有 `js_backend.o`（空壳）与 `js_backend_quickjs.o`

`BACKEND_UNIT` 是**二选一**（[check.sh:44-49](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L44-L49)），`IMPL_UNITS` 只放选中的那一个（[check.sh:229](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L229)）。quickjs 档 **没有** `js_backend.o`。结论（对象数不固定为 4）碰巧仍对，但理由是错的；按这句去写「等 5 个对象」的夹具会直接红。

### 4.7 `SMOKE OK` 是谁打印的（可不可能不真跑）

`SMOKE OK` 由 [smoke.c:104](file:///Users/samxx/trae-ws/nexcore/tests/smoke.c#L104) 打印，前面有 15 个 `return n` 断言（create/send/stat/drain/sink/shutdown/free 全链）——**内容本身不可伪造**，不是「打印一行字就算过」。风险不在 `SMOKE OK`，在「它链接的是谁」：见 §4.3。另注意 E5④ 没规定档位，null 档下它压根到不了第 104 行。

### 4.8 此处站得住

- `ar d NexCore.o` 的负例方向对，且 `ar d` 对不存在成员返回非零，不会「删了个寂寞还绿」。
- E5③ 允许「归档自身已定义的符号集合」是必要的：实测 `NexCore.o` 的未定义表就是 `_queue_*` 与 `_js_backend_*`，不放行这一条则永远红。
- `nm -u` 会列出 **weak undefined**（实测 `w` 符号 `_nx_evil` 出现在 `nm -u` 里），所以「用 weak 符号夹带非白名单依赖」这条绕过路径**不成立**。
- `.a` 作为产物本身合理；它确实消掉了主张席 A5 的「dylib 相对 install_name、拷走即崩」问题。

---

## 5. 攻点四：`hosts/` 纳入扫描

### 5.1 只扫 `*.ct`，与 C 宿主零交集（A16）

[check-boundaries.sh:58](file:///Users/samxx/trae-ws/nexcore/tools/check-boundaries.sh#L58)：

```bash
done < <(find "$dir" -name '*.ct' -type f | sort)
```

规则 1（`#if`）与规则 2（`errno`）只作用于 `.ct`。而裁定 §5 与卡面 §8 都明确**宿主用 C 写、`hosts/` 里没有 `.ct`**（[card.md:174](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L174)）。所以：

- 对真实交付物（`hosts/shell/main.c`）：**既不扫也不判**；
- E6 用来证明「覆盖」的那个 `hosts/shell/*.ct` **按设计永远不会存在**，是合成用例；
- 这条门禁在 PH-11 之后仍然不会对任何一行真实代码变红。

说「扫了但不阻断」还轻了：它阻断的是一个不会出现的文件类型。

### 5.2 T6 的验收方式与 E7 互斥（实测）

T6 要求「以 `scanned` 计数增长证明」扫到了 `hosts`（[card.md:129](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L129)）；E7/T7 要求 `find hosts -name '*.ct'` 为空（[card.md:148](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L148)、[card.md:130](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L130)）。没有 `.ct` 就没有 `scanned` 增长。实测：

```
$ bash tools/check-boundaries.sh src experiments/src hosts      # hosts 不存在
boundaries: PASS（19 个文件，无 #if / 无 errno / import 顺序正确）
$ bash tools/check-boundaries.sh "$PWD/src" "$PWD/experiments/src" "$PWD/hosts"
boundaries: PASS（19 个文件，...）                              # 仍 19
```

T7 落位后 `hosts/` 只有 `README.md`，计数仍是 19。T6 的 verify 命令在 T7 成立的前提下**无法通过**；要让它通过就得先放一个 `.ct`，又违反 E7。**这两条任务清单条目按现文不能同时收口。**

补充（基线校准）：T6 已落地的做法是独立脚本 [hosts_boundary_scan.sh](file:///Users/samxx/trae-ws/nexcore/tests/hosts_boundary_scan.sh#L10-L14)，往真实 `hosts/shell/` 注入 `__boundary_probe.ct` 再删，**绕开了 `scanned` 计数**，所以实测能红。这证明卡面的 T6 措辞该改；但合成 `.ct` 的性质没变——真实交付物仍是 C，见 §5.1。

### 5.3 缺目录静默跳过

[check-boundaries.sh:26](file:///Users/samxx/trae-ws/nexcore/tools/check-boundaries.sh#L26) `[ -d "$dir" ] || continue`。卡面 §4.4 明确「不得为了扫得到而强制目录存在」。这本身合理，但它意味着：如果将来 `hosts/` 被删/改名，门禁安静退回 19 个文件，没有任何提示。而 T7 只保证「今天存在」，没有判据保证「以后还在」。

### 5.4 即便出现 `.ct`，也没人校验它

`⑤a`（import 规则）只扫 `$SRC`（[check.sh:290](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L290)），`⓪` 生成器只读 `units.json` 里的 `src/`。`hosts/x.ct` 既不进 `IMPL_UNITS`，也不进 `⑤a/⑤b`。它能被检查的只有「有没有 `#if`/`errno`」，其余单元级规则全部缺席。这时它是一条「红得没有意义、绿得也没有意义」的检查。

### 5.5 此处站得住

- 主张席把 D4 定为 M2（不阻塞 PH-11、但补做会跨归属）是**对的**；裁定 §7.1 要求「同一次提交里扩扫描面」也是对的——只改默认值会被 `check.sh:401` 的显式实参二次覆盖，这一点我复核无误。
- `hosts/` 目录落位本身无害；E7 用 `find` 保证「不引入 `.ct`」也合理，只是它与 T6 冲突（§5.2）。

---

## 6. 攻点五：四件是否最小集

### 6.1 该补进 D1 的两件、可并进 D2 的一件（A17 缺项）

| 缺项 | 为什么属于 PH-10 而不是 PH-11 | 最小落法 |
| --- | --- | --- |
| job 异常 / Promise 拒绝可见性 | **D1 自己新开了一条静默路径**（§3.4）；PH-11 的错误面是为「语法错」设计的，不会自动覆盖「eval 成功、job 抛错」 | `pump` 把 `ran` 与异常计数回填到 `NX_STAT_JS_EXCEPTIONS`（键 17 已存在），并把 `-1` 反映到 `last_error` |
| `NX_STAT_JS_JOBS` 恒 0 | 泵已经在数 `ran`，但 `nexcore_stat` 对键 16 永远返回 0（[NexCore.ct:337-338](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L337-L338)） | `Backend` 暴露一个计数，`stat` 读它；同时让 E1 能断言「跑了 ≥1 个 job」，不再只靠副作用 |
| `shutdown` 前补一次泵 | 现在 shutdown 只 `pump_inbox`（[NexCore.ct:275](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L275)）；inbox 空就直接 `free_state`，待跑 job 一起没了 | `shutdown` 在 `Backend.shutdown` 前 `pump(h, 0)`；是否需要设上限留给 PH-11 |

这三条都不碰 ABI（不新增导出、不加结构体字段），符合 I-1/I-2，属「同一阶段顺手做掉」的量级。

### 6.2 `turns()` 僵尸（A17）

`turns` 在接口、映射、两个实现里都有（[js_backend.h.ct:20](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend.h.ct#L20)、[units.json:13](file:///Users/samxx/trae-ws/nexcore/src/units.json#L13)），但 `grep -rn "\.turns\|Backend.turns\|js_backend_turns" src/` **零调用**：

```
$ grep -rn "\.turns\|Backend.turns\|js_backend_turns" src/ | grep -v "\.h\.ct\|_map"
(无输出)
```

它既不在 ABI（不导出），也不是死代码（`⑤b` 要求 `units.json` 与 `pub` 函数集合一致，删它要同时改 4 个文件）。PH-10 恰好在这条接口上加 `pump`，是唯一低成本清理窗口：要么删（同时改 `units.json`/map/两实现），要么并进 `pump` 的返回值/`JS_TURNS` 统计。留着它，下一个读接口的人会以为「turn 与 pump 是两个概念」，而实际只有一个。

### 6.3 可减：`native_names` + `native_names_len`（过度设计候选）

主张席 §4.6 说 4 个字段减不到 3，理由是「长度必须由宿主给，库不得 `strlen`」。这条**只对「必须由库解析的名字列表」成立**。但库并不需要解析它——挂载完全可以在 `bootstrap_js` 里做：

```js
// 宿主自己的 bootstrap，不需要任何新字段
Shell = { print: function(){ return __nx_native("Shell.print", JSON.stringify([...arguments])) } };
```

`__nx_native` 只需要在 `native_call != 0` 时安装。于是 4 字段 → 2 字段（回调 + user），ABI 尾巴少 16 字节永久占用，而且顺手消掉 §2.5 那个「白名单不生效」的歧义。

只有当「白名单要作为访问控制」时，这两个字段才值得留——那就按 §2.5 的方式真正校验它。**现在是「付了 ABI 的钱，没买到控制」，两头不占。**

### 6.4 JSON 序列化在挂载层抛错，形成新的静默无响应

挂载包装是 `JSON.stringify(Array.prototype.slice.call(arguments))`（[quickjs:47](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L47)）。遇到 `BigInt` 或循环引用时 `JSON.stringify` **在进入 `__nx_native` 之前**就抛，于是 `JS_Eval` 返回异常 → eval 返回 -1 → `pump_inbox` 只写 `last_error`、不推 outbox（[NexCore.ct:203-208](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L203-L208)）→ 宿主看到 `count=0`。这和 §3.4 是同一类问题：D2 自己引入的失败，要等 PH-11 的错误面才可见。最小修法：挂载层 `try/catch`，失败时返回一个 `{__nxError:...}` 而不是抛。

### 6.5 此处站得住

- 「D1+D2+D3 阻塞、D4 闭合」的分档成立（主张席 §2.4 的删除测试我复核后同意）。
- 「程序模式求值、异常文案留给 PH-11」成立：它们不碰 ABI，宿主无感，放到有消费者的阶段做是对的。
- 「不导出 `nexcore_pump` / 不加 vtable」成立：会分别红 ⑩ 与 `①b`。
- 方案 A（C 宿主）的三条理由里，「C 只走公开 ABI」这一条在 `.a` 形态下被削弱（§4.2），但方向仍对。

---

## 7. 必须改的清单（逐条，带位置与可验证修法）

| # | 位置 | 现文 | 必须改成 | 依据 |
| --- | --- | --- | --- | --- |
| 1 | [card.md:98](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L98) | `struct_size` 小于 `offsetof(native_call)` 视为未提供 | `struct_size < offsetof(native_call) + sizeof(native_call)` 视为未提供；并补「88 这一档必须条件」 | §2.2 ASan |
| 2 | [NexCore.ct:89](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L89)（实现） | `>= NEXCORE_CONFIG_CURRENT_SIZE` | 同上，按字段偏移判，不用会漂移的 `CURRENT_SIZE` | §2.3 |
| 3 | [native_provider.c:174](file:///Users/samxx/trae-ws/nexcore/tests/native_provider.c#L174) | `malloc(sizeof(struct NexCoreConfig))` 冒充老调用方 | 真的只分配 `NEXCORE_CONFIG_MIN_SIZE` 字节，并补 88 档；ASan 下跑 | §2.4 |
| 4 | [check.sh:641](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L641) | 失败文案声称覆盖「越界读了尾字段」 | 去掉该声称，或按 #3 让夹具真能抓越界 | §2.4 |
| 5 | [card.md:144](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L144) E3 | 只测 `MIN` | 增加 `MIN+16`（88）这一档 —— 今天真实存在的旧尺寸 | §2.2 |
| 6 | [js_backend_quickjs.ct:114](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L114) | `native_call(name, ...)` 不校验白名单 | 校验，或把 D2 措辞改为「挂载清单，权限由宿主判」 | §2.5 |
| 7 | [card.md:96-102](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L96-L102) | 未写 `native_names` 生命周期与 `name` 语义 | 写明「白名单仅在 create 期间有效/库必须拷贝」「回调收到全名」 | §2.6 |
| 8 | [card.md:142](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L142) E1 | 「两档都要 PASS」 | 改为「quickjs 档 PASS；null 档 N/A 且不计通过」 | §3.2 |
| 9 | [card.md:68-71](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L68-L71) | `max_jobs` 语义只写注释 | 补「`max_jobs>0` 的剩余 job 归属」与「`<=0` 允许无界」两条显式约束 + 一个 `>0` 夹具 | §3.3 |
| 10 | [card.md:146](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L146) E5④ | 「只链 `.a` 跑出 SMOKE OK」 | 拆成「null 档可链接并跑到 drain 断言」+「quickjs 档链 `.a`+`libquickjs.a` 跑出 SMOKE OK」 | §4.1 实测 |
| 11 | 同上 E5② | 「含全部 7 个」 | 加「且不得有其他 `T` 符号」（或显式豁免内部前缀并说明理由） | §4.2 实测 19 个 |
| 12 | 同上 E5⑤ | 「链接必须失败（rc≠0）」 | 断言 stderr 含 `_nexcore_create`，并禁 `-undefined dynamic_lookup` | §4.4 实测 |
| 13 | 同上 E5① | 「对象集合 == …」 | 排除 `__.SYMDEF SORTED` | §4.5 实测 |
| 14 | [card.md:110](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L110) | 「同时有 `js_backend.o` 与 `js_backend_quickjs.o`」 | 改为「二选一，对象数随档变化」 | `check.sh:44-49/229` |
| 15 | [card.md:129](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L129) T6 | 「以 scanned 计数增长证明」 | 改为「向 `hosts/` 注入临时 `.ct` → rc=1；删除后 rc=0」，不要求计数增长 | §5.2 实测 19/19 |
| 16 | [card.md:54](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L54) D4 | 「`hosts/` 纳入边界扫描」 | 注明「只覆盖 `.ct`；C 宿主不在本门禁范围」，或加 `.c` 的对应规则（但后者与裁定 §5 的 C 宿主选择冲突，需先裁定） | §5.1 |
| 17 | [card.md:51](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L51) D1 | 只做「泵到空」 | 把 job 异常/拒绝计数并入 D1（`JS_EXCEPTIONS`/`last_error`），或明写「PH-10 产出已知静默失败」 | §3.4 |
| 18 | [js_backend.h.ct:20](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend.h.ct#L20) | `turns()` 无调用者 | 删除，或并进 `pump`/`JS_TURNS` | §6.2 |

---

## 8. UNVERIFIED 与限制（不计通过）

| 项 | 状态 | 原因 |
| --- | --- | --- |
| quickjs 档的 E1/E3/⑮/⑯ 运行结果 | **UNVERIFIED** | 本机无 `experiments/third_party/quickjs` 源码；只有 `check.sh:52-54` 的「缺库即 rc=2」保护 |
| 32 位 / 非 LP64 布局 | **UNVERIFIED** | `cc -m32` 在本机 SDK 上失败（`missing required architecture armv4t` / `tapi error`） |
| 自续链永久挂死 | **UNVERIFIED（代码路径）** | 结论来自 `pump` 的循环条件与 bootstrap 求值模式；无引擎可跑 |
| `shutdown` 时 `JS_FreeRuntime` 对待跑 job 的具体行为 | **UNVERIFIED** | 需要 QuickJS 源码/运行期；本文只主张「库没有主动泵」，不主张 free 一定崩 |
| `hosts/*.ct` 在真实构建里的行为 | **UNVERIFIED** | 该文件按卡面不会存在；未在仓库创建 `hosts/`（避免与在途工作冲突） |

---

## 9. 本轮做了什么、没做什么

**做了**：读三份文档 + 主张席评审 + `src/`（`NexCore.h.ct`/`NexCore.ct`/`js_backend/*`/`units.json`）+ `tools/check.sh`、`check-boundaries.sh` + 4 个夹具；在 `/tmp` 生成发布头并用 `cc`/ASan 实测布局与越界；在 `/tmp` 独立构建 null 档归档，实测 `ar t`/`nm`/链接/`-lnexcore`/`dynamic_lookup`；实跑边界脚本的 19/19 计数；对主张席的 6 条主张逐条对打（§2.4/§3.2/§4.2/§4.7/§5/§6.5）。

**没做**（按纪律）：未改 `src/`、`tools/`、卡片、`abi/` golden；未建 `hosts/`；未跑全量 `check.sh`（另一会话在途，且用户要求门禁不得并发）；未提交实现；未 push、未打 tag。

**与主张席的分歧（只列三处实质）**：

1. 它把卡面阈值偏差定性为「判定口径不同」（§4.7）；本文实测证明按卡面写是**堆越界**（§2.2）。
2. 它认为「4 字段减不到 3」（§4.6）；本文认为 `native_names(+len)` 在「不做访问控制」的前提下可由 `bootstrap_js` 承担，**4 可减到 2**（§6.3）。
3. 它把 D3 视为 D2 正确性的前提（§4.5）；本文认为 `.a` 反而打开了内部符号（19 个 `T`），需要额外一条「宿主只用 7 个」的约束才闭合（§4.2）。
