# nexcore 对 CT 两套 OO 机制的盘点与改造规划

> 只读盘点 + `/tmp` 最小样例验证。**未改 `src/`、未改 `.ai-engineering`、未改 `roadmap.md`、
> 未改任何既有验收条目**；未 push、未打 tag。
>
> 事实基线：仓库 HEAD `ced05d4`（2026-11-15）。编译器 `ct v0.44.2`
> （`PATH="$HOME/.local/ct/0.44.2:$PATH"`，见 [docs/toolchain.md:5-13](../toolchain.md)）；
> 系统 `ct` 为 v0.49.0，只在「命名空间固有静态方法」一项复跑过（见 CT-DEF-001）。
> 探针源码已落在 [experiments/oo_shim/](../../experiments/oo_shim/README.md)，可逐条重建。

---

## 0. 结论先行

| 问题 | 结论 | 证据 |
| --- | --- | --- |
| 现在用了哪套 OO 机制？ | 只用了 **SHIM 映射单元**（2 个静态 trait）；`impl`/`use Trait`/接口值在 `src/` 里**一处都没有** | §1.1、§1.2 |
| 档位切换靠什么？ | 构建脚本按档选实现文件名，另有一次**特例头构建** | §1.3 |
| 能否用映射单元替掉档位切换？ | **部分可以**：消费者源码与生成 C 可做到两档逐字节相同；但**档位选择仍留在构建调用里**，无法消掉 | §2 |
| 两份映射指向两套实现行不行？ | 行，但两份映射**不能同时**在 import 路径上（CT 硬报歧义），每次构建只能挂一个根 | §2.2 |
| 该用 vtable 吗？ | 只在「同一二进制里运行期多实现」时值得；对 null/quickjs 这组**链接期**差异，vtable 只加开销、不解决问题 | §2.4、§3.3 |
| 哪些不该 OO 化？ | ABI 结构、7 个导出函数、sink 回调、stat 键分发、队列热路径、错误码映射、protocol 谓词 | §4 |

一句话：**把「接口」稳定下来用 SHIM，把「实现」按档分开；vtable 留给真正的运行期多态。**

---

## 1. 现状盘点（文件 + 行号）

### 1.1 已经是映射单元（静态 trait / SHIM）

只有两处，且都是 `tools/nexcore_gen.py` 从 [src/units.json](../../src/units.json) 生成的：

| 单元 | trait / map | 实现单元 | 消费者调用点 |
| --- | --- | --- | --- |
| `queue` | [queue_map.m.ct:8](../../src/queue/queue_map.m.ct#L8) `pub trait Ring`；[:16](../../src/queue/queue_map.m.ct#L16) `map trait Ring for namespace queue` | [queue.ct:15-104](../../src/queue/queue.ct#L15-L104) | [NexCore.ct:77,79,81,87,88,95,96,107,108,155,178,183,193,211,220,281,284,300,304](../../src/NexCore.ct#L77) |
| `js_backend` | [js_backend_map.m.ct:8](../../src/js_backend/js_backend_map.m.ct#L8) `pub trait Backend`；[:15](../../src/js_backend/js_backend_map.m.ct#L15) `map trait Backend for namespace js_backend` | [js_backend.ct:23-54](../../src/js_backend/js_backend.ct#L23-L54) 或 [js_backend_quickjs.ct:37-144](../../src/js_backend/js_backend_quickjs.ct#L37-L144) | [NexCore.ct:93,106,182,257,277](../../src/NexCore.ct#L93) |

消费者只写 `import queue::queue_map;` / `import js_backend::js_backend_map;`
（[NexCore.ct:6-7](../../src/NexCore.ct#L6-L7)），跨单元调用一律 `queue::Ring.*` /
`js_backend::Backend.*`，生成 C 是裸调用（[NexCore.ct:9-16](../../src/NexCore.ct#L9-L16) 的注释也是这个口径）。
生成器的规则与漂移检查在 [nexcore_gen.py:150-181](../../tools/nexcore_gen.py#L150-L181)（渲染）、
[:215-256](../../tools/nexcore_gen.py#L215-L256)（④ recipe callee 校验）。

### 1.2 vtable 路径：`src/` 内 0 处

机械核对（`grep -n '^\s*(impl|use|pub trait|trait)\b' src/`）只命中两个生成的 `pub trait`；
没有任何 `impl`、`use Trait`、接口值、`_iface`/`_vtbl` 使用。

唯一的 `trait`/`impl` 实码在实验目录：[trait_abi.h.ct:10-73](../../experiments/trait_abi/trait_abi.h.ct#L10-L73)，
作用是钉死「`use Trait` 会追加隐藏 vptr、改变 struct 布局」这一 ABI 风险，
不是产品用法。ABI 侧也明确禁止：`NexCoreConfig` 不使用 trait
（[NexCore.h.ct:82-87](../../src/NexCore.h.ct#L82-L87)，决策 [D-12](../decisions.md#L234-L252)）。

### 1.3 靠构建脚本选文件（手工版 SHIM）

全部集中在 [tools/check.sh](../../tools/check.sh)：

| 行号 | 内容 |
| --- | --- |
| [35-61](../../tools/check.sh#L35-L61) | `JS_BACKEND="${NEXCORE_JS:-null}"`；`null → js_backend/js_backend`，`quickjs → js_backend/js_backend_quickjs`；注释写明「同目录、换实现文件」是 CT 约束下的唯一办法 |
| [229](../../tools/check.sh#L229) | `IMPL_UNITS=(NexCore queue/queue "$BACKEND_UNIT" protocol/protocol)` |
| [230-232](../../tools/check.sh#L230-L232) | **特例头构建**：先单独把 `src/js_backend/js_backend.h.ct` build 一次，注释解释「quickjs 档的实现文件只带自己的 .h.ct，不会产出 js_backend.h」 |
| [416-441](../../tools/check.sh#L416-L441) | null 档把 ⑧⑨/smoke 记 SKIP |
| [459-462](../../tools/check.sh#L459-L462) | ⑩：`libnexcore` 依赖里出现 QuickJS 即 FAIL（null 档的硬边界） |
| [611-618](../../tools/check.sh#L611-L618) | null 档 JS module load 记 UNVERIFIED |

这套做法的直接代价：

1. **两档实现同目录、同 namespace、同文件名前缀共存**（[js_backend.ct](../../src/js_backend/js_backend.ct)、
   [js_backend_quickjs.ct](../../src/js_backend/js_backend_quickjs.ct)），只编其中一个；
   两个头文件 [js_backend.h.ct](../../src/js_backend/js_backend.h.ct) 与
   [js_backend_quickjs.h.ct](../../src/js_backend/js_backend_quickjs.h.ct) 的签名重复。
2. **两档签名是否一致没有检查**：⑤b（[check.sh:364-398](../../tools/check.sh#L364-L398)）只校验
   `units.json` 登记的那一份 `.h.ct`；quickjs 头的四个 `pub` 声明没有对拍。
3. 需要 230-232 的特例头构建来绕开「quickjs 档不产出 js_backend.h」。

### 1.4 裸函数 + 手工分发的部分

| 位置 | 形态 | 说明 |
| --- | --- | --- |
| [NexCore.ct:38-47](../../src/NexCore.ct#L38-L47) | `static int32_t schema_hash_matches` | 单点使用，手工逐字节比较 |
| [NexCore.ct:168-198](../../src/NexCore.ct#L168-L198) | `static void pump_inbox` | inbox → eval → outbox 的手工循环 |
| [NexCore.ct:201-223](../../src/NexCore.ct#L201-L223) | `static void deliver_outbox` | 逐条回调 sink |
| [NexCore.ct:293-326](../../src/NexCore.ct#L293-L326) | `nexcore_stat` 的 if 链 | 按整数 key 手工分发；10..14 预留、15..18 已分配 |
| [NexCore.ct:22-24](../../src/NexCore.ct#L22-L24)、[155-163](../../src/NexCore.ct#L155-L163)、[178-197](../../src/NexCore.ct#L178-L197) | queue 返回码 → NX 错误码 | `NX_QUEUE_FULL -2` / `NX_QUEUE_EMPTY -4` 是**约定值**，不共享枚举 |
| [NexCore.ct:49-59](../../src/NexCore.ct#L49-L59) | `struct NexCoreHandleImpl` | `queue`/`outbox`/`js` 三个字段都是 `void *`，跨单元只传不透明句柄 |
| [queue.ct:15-103](../../src/queue/queue.ct#L15-L103) | 裸自由函数 | `struct Ring` 是单元内私有类型 |
| [protocol.ct:7-24](../../src/protocol/protocol.ct#L7-L24) | 裸自由函数 | **孤岛**：全仓库没有任何 `protocol::` 调用点（`grep -rn 'protocol::' src tests` 为 0）；只在 [check.sh:229](../../tools/check.sh#L229) 被编译 |
| [js_host.ct:1-11](../../src/js_host/js_host.ct#L1-L11) | 整份 deprecated 副本 | 注释写明「未注册进 units.json，不参与任一档构建，仅作回滚参照」；与 quickjs 实现逐行重复 |

---

## 2. 可行性验证（最小样例，均不碰 `src/`）

环境：`/tmp/nxo*` 五个工程；源码与命令已复制到
[experiments/oo_shim/](../../experiments/oo_shim/README.md)。全部用 `ct v0.44.2`。

### 2.1 两份映射单元 + 同一接口名

`p1_two_maps`：`null/jb/jb_map.m.ct` 与 `quick/jb/jb_map.m.ct` 声明**同名 trait、同 namespace**，
recipe 分别指向 `jb_null_*` / `jb_qjs_*`；消费者只写 `import jb::jb_map;`。

- 构建时只挂一个根（`-I null` 或 `-I quick`）：两次 rc=0，消费者源码一字未改，
  `app.c` 分别展开成 `jb_null_eval(...)` / `jb_qjs_eval(...)`。
- 两个根**同时**进 `-I`：CT 硬错，不是静默遮蔽：

```text
app/app.ct:4:1: error: unit 'jb/jb_map' exists in two import roots:
  'null/jb/jb_map.m.ct' and 'quick/jb/jb_map.m.ct'
```

结论：「同一接口名在不同映射单元下指向不同实现」**成立**，但每次构建只能挂一个映射根。

### 2.2 单份稳定映射 + 每档独立实现单元（推荐形态）

`p2_stable_map`：映射只有一份，recipe 指向固定 C 符号 `js_backend_*`；
`impl_null/` 与 `impl_quick/` 各有一对**无 namespace** 的 `jb_backend.ct` / `.h.ct`，
定义**同名 C 符号**。

实测（原始输出）：

```text
eval=-1 turns=0        # null 档
eval=42 turns=1        # quickjs 档
app_null/app.c == app_quick/app.c   （diff 无输出，逐字节相同）
```

这条最重要：**消费者源码与它生成的 C 都与档位无关**，差异被完全压在实现单元里。
它比 2.1 更好——契约只有一份，不会漂移。

### 2.3 跨单元 vtable + 稳定工厂符号

`p4_vtable_cross_unit`：`iface/jbif/jbif.h.ct` 发布 `pub trait Backend`、
`pub struct Handle { Backend js; }`、`pub Backend backend_create(void);`；
`jb_null/backend_null.ct` 无 namespace、`import jbif::jbif;`，
定义 `Backend jbif_backend_create(void)`（落码 `struct jbif_Backend_iface jbif_backend_create(void)`），
返回**堆对象**装箱。

实测 rc=0，输出 `turns=0 eval=-1 magic=7`；生成 C 里是
`h->js = jbif_backend_create();` 与 `h->js.vt->turns(h->js.self)`。

两条注意：

1. 返回**局部**接口值会悬垂（语言已知限制），工厂必须返回堆指针装箱。
2. 实现单元要 `import` 接口单元，当前 [check.sh:327-350](../../tools/check.sh#L327-L350) 的 ⑤a
   白名单**禁止**这种 import；采纳前必须先改 ⑤a。本次未改。

### 2.4 运行期 vtable 选择

`p3_vtable_runtime`：两个 struct `use Backend`，接口值放进
`struct Handle { Backend js; }`（堆上），按 `argv[1]` 运行期赋值。

实测：`./app` → `turns=0 eval=-1`；`./app quick` → `turns=1 eval=42`。
同一二进制内运行期切换成立。

但对本仓库的档位问题，它**不是**替代方案：要让两个实现都能被运行期选中，就必须同时链接
QuickJS，而 ⑩（[check.sh:459-462](../../tools/check.sh#L459-L462)）明确禁止 null 档依赖 QuickJS。
只链一个实现时，vtable 只是给每次调用加一次间接跳转。

### 2.5 固有方法（`impl Ring { ... }`）的可用边界

后续复核修正了本节最初的结论：**不是「跨单元不可用」，而是「命名空间单元里的静态方法不可用」**。
最小复现、两版原始输出与完整边界矩阵见
[CT-DEF-001](ct-defect-namespaced-inherent-static.md)（[复现目录](../../experiments/oo_shim/issue_inherent_cross_unit/)）。

实测边界（`ct v0.44.2` 与 `v0.49.0` 一致）：

| 单元有 `namespace` | 调用形态 | 同单元 | 跨单元 |
| --- | --- | --- | --- |
| 有 | `Ring.open(4)`（静态） | FAIL | FAIL |
| 有 | `r.get()`（实例） | PASS | PASS |
| 无 | `Ring.open(4)`（静态） | PASS | PASS |

原始报错是 `use of an undeclared variable`，但生成头里方法已导出为
`int ring_Ring_open(int n);`——失败在调用点的名字解析，不在可见性。
另外 `impl` 内的方法**必须带函数体**，只写原型报 `unexpected token`。

结论：命名空间单元不能用静态固有方法做构造入口；实例方法可用，但消费者要持有带类型的
接收者。队列的 `void *` 不透明句柄下，SHIM 静态映射仍是正确选择（理由见 §4）。

### 2.6 为什么「不动构建脚本」做不到

五条硬约束，逐条都有出处：

1. **QuickJS 是链接期依赖**，null 档不得出现（⑩，[check.sh:459-462](../../tools/check.sh#L459-L462)），
   所以两档不可能共用一个链接产物。
2. **CT 无条件编译**：`.ct` 里 `#if/#ifdef` 被
   [check-boundaries.sh:5](../../tools/check-boundaries.sh#L5) 与
   [:31-36](../../tools/check-boundaries.sh#L31-L36) 直接判违规；
   `.m.ct` 的 recipe 是静态文本，没有「按变量选 recipe」的语法。
3. **import 解析是单次文件查找**，两处同名直接报歧义（§2.1），没有优先级或版本选择。
4. **`ct build` 的实现单元是显式入参**（[check.sh:229-235](../../tools/check.sh#L229-L235)），
   import 不带档位元数据。
5. **`.m.ct` 零产物**，不能承载「选择逻辑」（[language-spec §4.10](../../../ct-lang/docs/language-spec.md) 的单元矩阵）。

因此可行边界是：**消费者与接口稳定，档位选择缩成构建入参里的一个路径**；
把「选哪个文件」换成「选哪个实现单元」，但不可能把这一选择从构建调用里消掉。

---

## 3. 改造方案（分阶段，均可独立回滚）

> 纪律：每阶段一个小提交（`git commit --only <具体文件>`），提交前两档门禁各自全绿；
> 不 push、不打 tag；不改判据、不改 golden 凑绿。

### S0 记录与前置（本次已做，不含代码改动）

- 探针与实测记录：[experiments/oo_shim/](../../experiments/oo_shim/README.md)。
- 需要新增一道**档位签名对拍**检查（新编号，建议 ⑮）：分别生成两档实现头的原型行，
  逐行相等；并给一个负例（改一个参数类型 → 必须红）。
  现在这条一致性**完全没有检查**（§1.3 第 2 点）。
- 判据：新检查能红能绿；不改既有 ①–⑭ 的判据。

### S1 后端实现迁出接口目录（核心改造）

改动的文件：

| 动作 | 文件 |
| --- | --- |
| 新增 | `src/backends/null/js_backend_null.ct` + `js_backend_null.h.ct`（无 namespace，定义 `js_backend_init/eval/shutdown/turns`） |
| 新增 | `src/backends/quickjs/js_backend_quickjs.ct` + `js_backend_quickjs.h.ct`（同样的 C 符号名；仅它 `#include "quickjs.h"`） |
| 删除 | `src/js_backend/js_backend.ct`、`src/js_backend/js_backend_quickjs.ct`、`src/js_backend/js_backend_quickjs.h.ct` |
| 不动 | `src/js_backend/js_backend.h.ct`、`src/js_backend/js_backend_map.m.ct`、`src/units.json` |
| 改脚本 | [check.sh:35-61](../../tools/check.sh#L35-L61) 的两个值改成实现单元路径；删掉 [230-232](../../tools/check.sh#L230-L232) 的特例头构建（接口头改为无条件先构建） |

为什么这样就够：

- 映射 recipe 的 callee 仍是 `js_backend_*`；实现单元用无 namespace 的显式函数名提供同一组符号
  （§2.2 实测）。⑤a 的 self-import、②a 的产物断言、⑤b、⓪ 都不需要改判据。
- `js_backend.h.ct` 仍是唯一接口头，⑤b 与 ④ 的符号表来源不变。
- null 档只编 null 实现，⑩ 仍成立。

判据（缺一不可）：

1. `NEXCORE_JS=null bash tools/check.sh` 与 `NEXCORE_JS=quickjs bash tools/check.sh` 都 rc=0，
   末行 `ALL CHECKS PASSED`。
2. 两档的 `build/impl/NexCore.c` **sha256 相同**（消费者与档位无关的直接证据）。
3. 两档 `nm -g --defined-only` 里 `js_backend_*` 符号集合相同（⑮ 对拍）。
4. null 档 `otool -L build/bin/libnexcore.dylib` 无 quickjs（⑩ 原判据）。
5. 既有测试文件零改动；⑧⑨/smoke 在 quickjs 档仍 PASS、null 档仍记 SKIP（不计通过）。

退路：一个提交只动上表文件；回滚即 `git revert`，两份同目录实现恢复原状，映射与生成器未动。

注意：本机 `build/` 被封存，全量检查需按 [docs/toolchain.md:35-43](../toolchain.md) 在 `/tmp` 副本跑。

### S2 删除 deprecated 的 `src/js_host/`

[js_host.ct:1-11](../../src/js_host/js_host.ct#L1-L11) 自述「未注册、不参与构建、仅作回滚参照」，
且与 quickjs 实现逐行重复。删除前按注释指向的
`docs/reviews/nexcore-p2-card-draft.md §10` 核对前置条件；删除后全量门禁仍须两档全绿。
这一阶段与 OO 无关，只是把「同目录第二实现」的历史残留清掉。

### S3（可选，默认不做）vtable 运行期后端

触发条件只能是以下之一，且需要单独签字：

- 需要一个二进制同时跑两档（例如宿主可选 fallback）；
- 需要宿主在运行期注入后端（插件化）。

做法：接口头里加 `pub trait Backend` + `pub Backend backend_create(...)`；每档一个实现单元提供
**同名工厂 C 符号**；`NexCoreHandleImpl.js` 从 `void *` 改成接口值。

代价与前置：

- 每次 eval 多一次间接跳转；`js` 字段从 `void*` 变成两字宽胖指针。
- 若要在运行期二选一，两个实现必须同时链接 → ⑩ 必须改（null 档会带 QuickJS），
  属 ABI/验收边界变更，**必须用户签字**，不能自行改判据。
- 跨单元 trait impl 需要放宽 ⑤a（§2.3）。

结论：**当前两个功能没有这一需求，S3 只登记为备选，不排期。**

### S4 `protocol` 孤岛处置

[protocol.ct](../../src/protocol/protocol.ct) 没有任何调用点，却被编进每个档位。
两条路二选一，需产品侧定：

- 若协议信封校验近期不上：从 [check.sh:229](../../tools/check.sh#L229) 的 `IMPL_UNITS` 移除该单元；
- 若要用：新增 `protocol/protocol_map.m.ct`，由消费单元经 SHIM 调用（它本来就是纯谓词，
  适合静态映射而不是 vtable）。

---

## 4. 不该 OO 化的地方

| 位置 | 为什么保持现状 |
| --- | --- |
| `NexCoreConfig` 及所有跨 ABI 结构（[NexCore.h.ct:88-105](../../src/NexCore.h.ct#L88-L105)） | trait 会追加隐藏 vptr，平台侧布局静默错位；D-12 已冻结「跨 ABI 结构禁止 trait」 |
| 7 个 `nexcore_*` 导出函数（[NexCore.h.ct:113-125](../../src/NexCore.h.ct#L113-L125)） | C ABI 契约：符号名、调用约定、导出白名单（⑩ 恰好 7 个）。改成方法会改符号与布局 |
| `NexCoreSink` / `NexCoreLog`（[NexCore.h.ct:78-80](../../src/NexCore.h.ct#L78-L80)） | 必须是无 userdata 的 C 函数指针（上下文走 `void *`）；CT 闭包是 `(fn, env)` 两词 ABI，不能当 C 回调（bindings §3 / SH-67） |
| `nexcore_stat` 的 key 分发（[NexCore.ct:293-326](../../src/NexCore.ct#L293-L326)） | 冻结的整数契约（1..18 不改号）；表驱动已足够，vtable/注册表只会把编号藏起来 |
| queue 的 push/pop（[queue.ct:38-79](../../src/queue/queue.ct#L38-L79)） | 单实现、每消息热路径；vtable 只加间接寻址、不买任何多态；队列是命名空间单元，固有静态入口编不过（§2.5 / [CT-DEF-001](ct-defect-namespaced-inherent-static.md)） |
| queue 返回码 → NX 码映射（[NexCore.ct:22-24](../../src/NexCore.ct#L22-L24)） | 是跨单元契约，必须显式；抽象成「错误类型」会把策略藏进队列单元 |
| `schema_hash_matches`（[NexCore.ct:38-47](../../src/NexCore.ct#L38-L47)） | 单点使用、无状态、无多实现 |
| `protocol` 的两个谓词（[protocol.ct:7-24](../../src/protocol/protocol.ct#L7-L24)） | 纯函数；若保留，静态映射比 trait 对象更合适 |
| `js_host`（[js_host.ct](../../src/js_host/js_host.ct)） | 不是「该不该 OO」的问题，是死代码：应删除而不是 OO 化（S2） |

判断口径：**有状态 + 多实现 + 调用方需要面向接口** 才上 vtable；
**跨单元、单一实现、性能敏感** 用 SHIM；**同单元、类型确定** 用固有方法或裸函数。

---

## 5. 其他最新 CT 特性（非 OO，仅登记）

| 特性 | 对 nexcore 的用法 | 结论 |
| --- | --- | --- |
| `defer { }`（P38） | `nexcore_create` 的三段重复清理（[NexCore.ct:80-99](../../src/NexCore.ct#L80-L99)、[105-111](../../src/NexCore.ct#L105-L111)）可收敛 | 可评估，但 `.h.ct` 内禁用 defer；属独立小改，不混进 S1 |
| `errdefer` / `?`（P40/P44） | 仅内部 helper 可能用；ABI 返回码必须保持 `int32_t` | 不建议用在 ABI 路径 |
| `atomic T`（P41） | worker 线程（S6）落地后用于取消标志/计数 | 现在不用 |
| `Str` / `Option` / `Result`（P34/P36/P47） | 可作 protocol 解析的内部类型；不进 ABI | 现在不用 |
| `ct build --lib`（P39） | 与现有手写 dylib 导出面的关系需另案 | 不在本规划内 |

容器限制要提前记住：**接口值和带 vptr 的 struct 不能进容器**（L29-13），
所以「`Vec<Backend>` 装一组后端」这条路现在写不出来，规划里不要假设它存在。

---

## 6. 未验证 / 边界

- **未跑全量 `tools/check.sh`**：本机 `build/` 封存，且工作树有其他会话在改 `src/`
  （HEAD `ced05d4`，未跟踪 `tests/pump_microtask.c`）。本报告所有实测都来自 `/tmp` 最小工程，
  产品仓库未构建、未提交。
- `ct v0.44.2` 与 `v0.49.0` 的差异未系统扫描；§2.5 一项在两端行为一致，其余未扫。
- S1 的两档 `NexCore.c` 同 sha 是**基于 §2.2 的等价推演**，尚未在真实 `src/` 上实测；
  落地时以判据 2 为准，不成立就不算通过。
- S3 的跨单元 vtable 已验证最小形态，但把它接进 `NexCore.ct` 需要同时改 ⑤a 与（若运行期二选一）⑩，
  这两项都是判据变更，必须单独签字，本次不做。
