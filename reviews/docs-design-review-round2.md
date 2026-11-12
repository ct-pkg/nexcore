# docs v0.3 整改复核（第二轮）

- 复核对象：`docs/` v0.3 整改稿（acceptance 157 行、design 486 行、async 366 行、codegen 640 行、decisions 357 行、protocol 356 行、open-questions 299 行、README 67 行）
- 复核方式：只读 `docs/`；实验只在 `/tmp`；本报告只写 `reviews/`
- 结论：**不通过**。P0 4 条（其中 1 条随 D-15 撤回而失效）、P1 8 条、P2 5 条
- 做对的部分：编号迁移干净、57 条计数正确、关闭顺序已收敛到 async §7.1 单一权威（详见 §4、§5）

分列：

| 级别 | 方案缺陷 | 文档表述问题 |
|---|---|---|
| P0 | 3（P0-1/P0-2/P0-3）+ 1 条件项（P0-4） | 0 |
| P1 | 4（P1-5/P1-6/P1-7/P1-8） | 4（P1-1..P1-4） |
| P2 | 0 | 5 |

---

## 1. 第一优先：`.m.ct @map` 路线实测（推翻 D-15 的排除前提）

### 1.1 最小工程

目录：

```text
/tmp/ctmap/
  main.ct              # 根单元：无 namespace，只 import a、b，做 ABI 入口与构建入口
  a/
    a.h.ct             # 子单元 A：namespace a
    qmap.m.ct          # A 的映射单元：把 a::Y.call 映射到 a_abase
  b/
    b.h.ct             # 子单元 B：namespace b
    qmap.m.ct          # B 的映射单元：把 b::X.call 映射到 b_bfn
  out/                 # ct build 产物
```

> 用户要求「四个文件」；实际最小可编译形态是 **2 个实现单元 + 2 个映射单元 + 1 个根单元**。映射单元是每个目标命名空间一份，不能省略；这是该路线的真实最小成本。

文件实际内容：

`a/a.h.ct`：

```c
import b::qmap;

namespace a;

pub int abase(int x) {
    return x + 100;
}

pub int afn(int x) {
    return b::X.call(x) + 1;
}
```

`a/qmap.m.ct`：

```c
#include "a.h"

namespace a;

pub trait Y {
    int call(int x);
}

map trait Y for namespace a {
    call(x) -> a_abase(x);
}
```

`b/b.h.ct`：

```c
import a::qmap;

namespace b;

pub int bfn(int x) {
    return x * 2;
}

pub int bfn2(int x) {
    return a::Y.call(x);
}
```

`b/qmap.m.ct`：

```c
#include "b.h"

namespace b;

pub trait X {
    int call(int x);
}

map trait X for namespace b {
    call(x) -> b_bfn(x);
}
```

`main.ct`：

```c
import a;
import b;

int main(void) {
    if (a::afn(5) != 11) return 1;
    if (b::bfn2(5) != 105) return 2;
    return 0;
}
```

### 1.2 构建命令与退出码

```text
$ ct build --emit-c --ns-root . -I . -I a -I b main.ct -o out
build_exit=0
cc -std=c99 out/main.c out/a.c out/b.c -o a.out
```

产物：`out/a.c out/a.h out/b.c out/b.h out/main.c`。

### 1.3 子单元 A 调子单元 B 的展开前后对照

| 位置 | 展开前（CT 源） | 展开后（生成的 C） |
|---|---|---|
| `a/a.h.ct:10` → `out/a.c:14` | `return b::X.call(x) + 1;` | `return (b_bfn((x)) + 1);` |
| `b/b.h.ct:10` → `out/b.c:14` | `return a::Y.call(x);` | `return a_abase((x));` |

include 也由映射单元的 prologue 带入消费者 `.c`：

```c
/* out/a.c 顶部 */
#include "b.h"

#include "a.h"
```

```c
/* out/b.c 顶部 */
#include "a.h"

#include "b.h"
```

两个目标头都由 `ct build` 生成，且带 include guard：

```c
/* out/b.h */
#ifndef B_H
#define B_H
int b_bfn(int);
int b_bfn2(int);
#endif
```

### 1.4 A 和 B 都是命名空间单元吗

是。`a/a.h.ct` 的 `namespace a;`、`b/b.h.ct` 的 `namespace b;` 都在文件内；根 `main.ct` 没有 namespace，只作为构建入口与 ABI 入口。**结论不降级**：这是两个命名空间单元之间的双向调用，不是「一个根单元调子单元」。

### 1.5 双向、include 顺序、循环依赖

- 双向：A→B（`a::afn` 调 `b::X.call`）与 B→A（`b::bfn2` 调 `a::Y.call`）都在同一次 `ct build` 中编译通过。
- 严格编译与运行：

```text
$ cc -std=c99 -Wall -Wextra -Werror -I out out/main.c out/a.c out/b.c -o out/app
cc_exit=0
$ ./out/app
run_exit=0
```

- 模块图无环：根 import `a`、`b`；`a.h.ct` import `b::qmap`；`b.h.ct` import `a::qmap`；两个映射单元不 import 任何实现单元（只在自己的 prologue 里 `#include "对方.h"`）。所以 import 图是 `root → a,b → mapping`，没有 `a → b → a` 的实现单元环。
- C include 无环：`out/a.c` 先 include `b.h` 再 include `a.h`；`out/b.c` 反之。两个 `.h` 只含函数原型，不互相 include，因此不存在头文件循环。顺序差异不影响结果，因为两侧都先拿到目标声明。
- 条件：映射单元只能 import 另一个映射单元（CT 语言规范 §4.10），这条恰好阻止了「映射单元再去 import 实现单元」造成的环。

### 1.6 对生成器的要求（GEN/SYM 该写什么）

这条路线可行，但需要把「根转发」换成「映射单元 + cname recipe」。建议新增两条规则（编号避让现有 GEN-19/SYM-04）：

- **GEN-20（映射单元生成）**：为每个被跨单元调用的命名空间单元生成 `<ns>/<ns>_map.m.ct`：
  - 文件头 `#include "<ns>.h"`；
  - `namespace <ns>;` 必须在 import 之后、声明之前（GAP-02）；
  - 每个跨单元函数一个静态 trait 方法，recipe 的 callee 必须是符号表里的 **cname**（如 `b_bfn`），不得由生成器拼字符串；
  - 映射单元不得 import 实现单元，只能 import 其他映射单元。
- **SYM-05（跨单元调用改写）**：命名空间单元内的跨单元调用一律生成为 `<ns>::<Trait>.<method>(...)`，并在文件头生成 `import <ns>::<ns>_map;`；根单元不再承担转发。

机械检查（都可脚本化或由编译器判定）：

1. `ct build --emit-c` 退出码 0；
2. 每个生成 `.c` 过 `cc -Werror=implicit-function-declaration -fsyntax-only`；
3. `grep -nE '::' schema/generated/c` 为空（生成 C 只有 cname）；
4. 每个 recipe 的 callee 在目标 `<ns>.h` 中存在（符号表断言，不靠正则）；
5. `import` 解析后按后缀检查：根单元可 import `.h.ct`/`.ct`；命名空间单元只允许 import `.m.ct`；
6. 负例：命名空间单元直接写 `<ns>::fn(...)`（不走映射）必须失败——CT 会报 `no function of that name in that namespace`。

限制（必须写进设计，否则会误以为映射万能）：

- 映射不能取函数地址。`&ns::f`、`= ns::f` 仍然失败（GAP-05）。回调注册仍需在**调用方单元**生成 `static` wrapper，wrapper 内部用映射调用目标；这与 GEN-16 一致，不需要根单元。
- 被跨单元调用的函数必须是 `pub`，否则目标 `.h` 不声明 cname。
- 每个跨单元函数多一行 trait 方法与 recipe；这是显式的、可逐行审阅的成本，不是运行时开销。

### 1.7 结论

D-15 的「(b) 是唯一同时满足可编译和可审查的方案」**不成立**。`.m.ct @map` 不是「只解决符号映射、不解决 include 与限定名解析」：prologue 的 `#include` 正是解决 include，`map` 正是把限定调用改写成 cname。实测为两个命名空间单元、双向、`cc -Werror` 通过、运行通过。

因此有三个选项，而不是一个：

- **(b) 原样**：根协调单元集中转发。代价如 D-15 所述。
- **(b') 映射单元**：根只做 ABI 与构建入口；子单元之间用 `.m.ct` 映射互调，回调用本地 wrapper。**已实测可编译、可运行。**
- **(b'') 混合**：默认映射；只有映射表达不了的形态（如需要把函数指针跨单元传递且无法在调用方生成 wrapper）才回根转发。

建议 D-15 重开，至少把「唯一可行」改成「(b) 与 (b') 均可行，按审查成本选择」，并补上 (b') 的实测附录。若仍选 (b)，理由不能再用「只有它能编译」。

---

## 2. 第二优先：三个实现层 P0

### P0-1（方案缺陷）：`js_done` 的等待语义会 UAF

**触发路径**：

1. 非 JS 线程（含 drain 线程）调 `nexcore_shutdown`；
2. JS 线程关闭 turn 走到步骤 3.7，置 `js_done` 并 `cond_signal`；
3. 调用方走 async §7.1 步骤 4 的「`thread_join(js_thread)` **或** 等 `js_done`」中的后者；
4. 调用方进入步骤 5，置 `state=DEAD`，返回 0；
5. 平台随即调 `nexcore_free`；`free` 只检查 `state==DEAD && drain_active==0`，在同一把锁内销毁句柄；
6. JS 线程此时可能仍在关闭 turn 的尾部（步骤 5 或返回路径），继续读句柄、`cond_signal` 或释放 runtime 相关资源 → **use-after-free**。

**危害**：UAF、mutex/cond 二次销毁、`free` 返回后仍有 JS 线程触碰句柄；违反 async §7.1 的「`free` 返回后不再触碰任何句柄资源」。

**冲突位置**：

- async §7.1 步骤 3.7（置 `js_done`）与步骤 4（「`thread_join` **或** 等 `js_done`」）与步骤 5（置 DEAD）；
- design §7 L427：`free` 只检查 `state==DEAD && drain_active==0`；
- async §7.1 步骤 5 还说「若第 4 步是 JS 线程内部发起，则由该线程在关闭 turn 结束时设置 DEAD」——即 DEAD 有两个写入方。

**建议修法**：

- 非 JS 路径只允许 `thread_join(js_thread)`，`js_done` 只用于唤醒等待者，不能作为「线程已退出」的替代；
- `free` 增加 `js_thread_joined` 断言；未 join 返回 `NX_E_BUSY`；
- DEAD 只由 join 方在生命周期锁内设置；JS 内部路径改为 CAS 或「关闭 turn 结束时置 DEAD，join 方只等待不写」；
- RC 增加「drain 线程 shutdown 后立刻 free」与「JS 线程内部 shutdown + 外部线程同时 shutdown」两例。

### P0-2（方案缺陷）：JS 线程内部 shutdown 会被 interrupt 打成 `E_JS_RUNAWAY`

**触发路径**：

1. Core handler（运行在 JS 线程、位于某个 turn 或 pump 内）调用 `nexcore_shutdown`；
2. 步骤 1 置 `state=STOPPING`；
3. 步骤 4 走「JS 线程内部发起」分支：只置 STOPPING、发信号、**立即返回 0**；
4. 但 async §5.2 规定 `JS_SetInterruptHandler` 在 `stopping` 时返回非 0；
5. 下一次 interrupt 检查把当前脚本判为 `E_JS_RUNAWAY`；D-05 规定 `E_JS_RUNAWAY` 触发 **fatal shutdown**；
6. 关闭 turn 没有按 §7.1 执行，JS pending 没有以 `E_SHUTDOWN` 拒绝，`.catch` 不执行，状态进 FATAL 而不是 DEAD；
7. `shutdown` 已经返回 0，调用方却得到一个 FATAL 句柄。

**危害**：调用方按 0 认为「优雅关闭完成」，实际是 fatal；Promise 的 `.catch` 丢失；与 P1-2 的「预算耗尽」叠加时，调用方完全无法判断哪些 `.catch` 会执行。

**冲突位置**：

- async §5.2（「或 `stopping` 时返回非 0」）；
- async §7.1 步骤 4（JS 内部发起立即返回 0，关闭 turn 继续）；
- decisions D-05（`E_JS_RUNAWAY` → fatal shutdown）。

三者不能同时成立。

**建议修法**：

- 把 `stopping` 拆成 `stop_requested`（协作式，不接受新 turn）与 `abort_requested`（interrupt 用）；
- interrupt 只响应 `abort_requested`、单脚本 50 ms、连续泵送 200 ms，不响应 `stop_requested`；
- 关闭 turn 内显式设置 `in_close_turn`，interrupt 在此期间只允许单脚本超时生效；
- 或者明确：JS 内部发起 shutdown 一律按 FATAL 处理，`shutdown` 返回 `NX_E_INTERNAL`，不得返回 0。

### P0-3（若 (b) 撤回则本条一并失效）：(b) 的转发机制未定义

**触发路径**：实现者按 design §3.4 L101-L102 接线：

- 根提供 `nexcore_forward_*`；
- 根向 A 注入回调/函数指针；
- A 调用注入的指针。

**缺失项**：

1. **接口定义**：注入的回调集合没有 struct/typedef/头文件；`nexcore_forward_*` 的签名没有给出。
2. **生命周期**：注入发生在 `nexcore_create` 的哪个阶段？A 的初始化与 B 的初始化谁先？根、A、B 的析构顺序是什么？
3. **存储位置**：L102 明确「不通过全局变量」，但 A 要在多次调用之间保存注入的指针，只能放全局变量或每次调用传 context；两条路都没有规定。若放 context，A 的每个公开函数都要加参数，接口形态完全改变。
4. **取址限制**：根不能对 `js_host::fn` 取地址（GAP-05：`&ns::f`、`= ns::f` 均失败）。根必须为每个回调生成**根本地 `static` wrapper**，再注入 `&wrapper`。设计只说「注入回调/函数指针」，没有说这一层；这会让根文件进一步膨胀。
5. **符号冲突与初始化顺序**：根 import 所有子单元后，跨单元 cname 冲突规则、各子单元 init 顺序均未定义；GAP-02 只定义了单文件内 `#include → import → namespace → 声明` 的顺序。
6. **生成器**：GEN-17/SYM-04 只「禁止命名空间单元跨单元调用」，没有任何规则生成 `nexcore_forward_*` 或注入代码；ACC-E-08 的「根单元经转发调用命名空间单元（正例）」没有定义输入与产物，无法判定。

**危害**：选择 (b) 无法从文档推导出可实现的形态；ACC-E-08 不可判定；「根单元成为耦合中心」的代价被写轻——真正的代价是**业务编排上移到根单元**、每个跨单元回调多一个根本地 wrapper、以及 init/teardown 顺序耦合。

**与文档冲突**：design §3.4 L101-L102 vs FIX-16/GEN-17 vs ACC-E-08 vs GAP-05。

**建议**：若采用 §1 的 (b')，本条一并撤回；若保留 (b)，必须补齐上述 6 项，并把代价改写为「编排上移 + 每回调 wrapper + 生命周期耦合」，而不只是「多一层转发」。

---

## 3. 关闭顺序复核

### 3.1 design 与 protocol 是否只做引用

- design §7 L412：明确「关闭顺序只在 async §7.1 定义一次，本节与 protocol §12.6 只引用，不重复步骤」——**通过**。
- protocol §12.6 L331-L340：只引用，但 L337 仍有「随后泵空，`.catch` 有机会执行」。这不是第二套步骤，但是第二套**可见行为描述**：它没有写「预算耗尽时剩余 `.catch` 不执行」。与 async §7.1 步骤 3.4 冲突。**P1（文档表述）**。

### 3.2 四类在途对象的收尾与计数

async §7.1 的表覆盖四类：

| 对象 | 收尾动作 | 计数 |
|---|---|---|
| JS pending | 以 `E_SHUTDOWN` 拒绝，随后泵 `.catch` | 无（不需要） |
| outbox 响应 | 丢弃，不调 sink | §7.1 表未写计数；§7.2 写「事件/响应」累加 |
| outbox 事件 | 丢弃，不调 sink | 累加 `NX_STAT_DROPPED_SHUTDOWN` |
| inbox 已接受未处理 | 丢弃，不执行 handler | §7.1 表写「累加计数」（未命名） |

`NX_STAT_DROPPED_SHUTDOWN` 在 design §6.4 L403 定义为「shutdown 时丢弃的 inbox/outbox 项数」。因此 ACC-C-05 的 `100 响应 + 100 事件 + 100 inbox = 300` 在**合并三节**后成立；但单看 §7.1 的表无法得出 300（响应行没写计数、inbox 行没写计数器名）。**P1（文档表述）**。

### 3.3 `NX_STAT_CLOSE_BUDGET_EXCEEDED` 的调用方语义

- async §7.1 步骤 3.4 写得很清楚：「记录 `NX_STAT_CLOSE_BUDGET_EXCEEDED`，停止泵送，继续关闭（剩余 reaction 不执行；任何 Promise 不会被二次 settle）」。
- 但这段在内部关闭顺序里；**protocol §12.6 与 design §7 都没有对调用方说「预算耗尽时部分 `.catch` 永不执行」**。平台侧只能靠读 async 的步骤表推断。
- ACC-C-11 只断言统计键与「无二次 settle」，没有断言「哪些 `.catch` 没执行」。
- **P1（文档表述）**：调用方可见契约缺失；建议在 protocol §12.6 加一句「预算耗尽时，未执行的 `.catch/.finally` 不会再执行；不得依赖它们做资源释放」，并在 acceptance 里用固定谓词断言「已执行的 `.catch` 数 < 100」。

### 3.4 `js_done` 判断时机的竞态

见 P0-1。补充：`js_done` 由 JS 线程在步骤 3.7 置位，但「关闭 turn 是否已经完全结束」与「线程是否已经退出」是两件事；文档把两者混在「等 `js_done` 或 `thread_join`」里。判断时机没有竞态的前提是**只使用 `thread_join`**；一旦使用 `js_done`，竞态就存在。

---

## 4. 编号迁移是否彻底

全库搜索旧前缀（`G-`/`R-`/`F-`/`D-` 裸编号）：

```text
README.md:52  编号一经发布不复用；删掉的条目在文档中保留说明（例如 `R-01..R-03` 已被 `SYM-01..SYM-04` 取代）。
README.md:66  新增 R-01..R-03 与待确认清单
codegen.md:547 原 R-01/R-02/R-03 已删除：...
```

这三处都是**历史说明或删除记录**，不是跨文档的现行引用；`D-xx` 是现行决策编号。除此之外没有裸 `G-`/`R-`/`F-`/`D-` 残留。`roadmap.md`、`acceptance.md`、`design.md`、`protocol.md` 的引用均为新前缀。

**结论：编号迁移彻底，通过。** 这三处不算残留，不应要求删除。

---

## 5. 57 条验收抽检（15 条）

计数校验：

```text
$ grep -oE 'ACC-[A-H]-[0-9]{2}' docs/acceptance.md | sort -u | wc -l
57
```

分组：A 11、B 8、C 10（C-01..C-09 + C-11，C-10 缺号）、D 4、E 8、F 5、G 6、H 5。

抽检 15 条：

| 条目 | 判定 | 依据 |
|---|---|---|
| ACC-A-04 | 条件可判定 | 「严格相等实现必须失败（防回归）」在最小长度语义下应写成「构造一个严格相等实现，喂更大 `struct_size`，必须失败」；当前措辞可被读成两种意思 |
| ACC-A-08 | 条件可判定 | 需要 barrier 固定「第一个 `drain(-1)` 已阻塞」；否则第二个线程可能在第一个返回后才调用，测试会 flaky |
| ACC-A-09 | 可判定 | 假 `now_ms` 推进 24h + 真实等待 ≤100 ms，均可测 |
| ACC-A-10 | 可判定 | golden diff，机械判定 |
| ACC-A-11 | 条件可判定 | 「JS 线程已回收」没有 ABI 查询；只能靠线程计数/ASAN 间接判，证据口径未写 |
| ACC-B-03 | 可判定 | 固定种子 + 可注入调度器 + 三档时序，断言「恰好 settle 一次」 |
| ACC-B-07 | 可判定 | 幂等返回 `null`；`E_NOT_SUBSCRIBED` 已从 protocol 删除且数值不复用 |
| ACC-C-03 | 条件可判定 | 10k continuation 与 async §5.2 的 200 ms 硬窗口可能冲突；慢设备上 10k 链可能被判 `E_JS_RUNAWAY`，验收未给预算豁免 |
| ACC-C-05 | 条件可判定 | `==300` 依赖 §7.1/§7.2/§6.4 三节合并；单看 §7.1 表推不出响应计数 |
| ACC-C-11 | 条件可判定 | 「500 ms + 200 ms」未定义 200 ms 的来源；与 `shutdown_budget_ms=500` 的关系不明 |
| ACC-D-03 | 可判定 | 「每个溢出→恢复批次恰好一条带 `dropped` 的 `E_BACKPRESSURE`」，机械判定 |
| ACC-E-04 | 可判定 | 自动规则列表 + GEN-10/GEN-14 人工签字，边界清楚 |
| ACC-E-08 | 不可判定 | 正例「根单元经转发调用命名空间单元」没有定义输入/产物；若改用 `.m.ct`，本条需重写 |
| ACC-G-05 | 条件可判定 | Q-16 设备未固化，p99 阈值仍「暂定」；含/不含 handler 的构造方式未写 |
| ACC-H-05 | 可判定 | QuickJS 抛异常、进程不崩、可 `shutdown/free` 重建，均可观测 |

统计：可判定 7、条件可判定 7、不可判定 1（E-08）。上一轮 51 条里 25 条条件/不可判定；本轮抽检的 15 条里仍有 8 条不是「一眼判真假」。**改善明显，但没有达到「全部改成数字或固定谓词」的声称。**

---

## 6. `bench-v1` 是否可复现

已定义的部分（D-17、acceptance §10）：

- 并发度 1（顺序往返）；10k warmup + 100k 采样；p50/p95/p99/max；32 并发单独测吞吐；
- 端到端包含 JS/Core handler，另报「不含 handler」分段；
- p99 线性插值；计时起点 `nexcore_send` 返回 0，终点 sink 收到响应；
- 协议、负载、预算、测量方法任一变化升 `bench` 版本。

未定义、导致换机不可比的部分：

1. **设备未固化**：Q-16 仍 `OPEN`，acceptance §10 写「型号待定」，阈值标「暂定」。
2. **离群值处理未定义**：没有说是否剔除、剔除比例、是否报告最大值。
3. **重复次数未定义**：只跑一轮 100k，没有 run-to-run 方差或置信区间。
4. **热状态/频率未定义**：macOS 没有 CPU 频率策略；没有预热到稳态的判据（10k warmup 是消息数，不是热平衡条件）。
5. **payload 种子值未写**：只说「固定种子生成的嵌套对象/数组」，没有种子值与 payload 文件哈希。
6. **吞吐采样未定义**：32 并发跑多久、采样多少、取总吞吐还是分片。
7. **「不含 handler」如何构造未定义**：是空 handler、no-op core、还是跳过 JS？三者数字不可比。
8. **p50/p95 插值未定义**：只写了 p99 线性插值。

**结论：同一台机器、同一构建、同一 payload 下可重复；换机器不可比。** D-17 只固化了「协议」，没有固化「环境与统计口径」。P1。

---

## 7. `NexCoreConfig` 的三个洞

### 7.1 `struct_size` 最小长度协商的「向前兼容」判据

D-18 写：

- `struct_size >= NEXCORE_CONFIG_V1_SIZE` 接受，尾部忽略；
- 小于最小值返回 `NX_E_INVALID_ARGUMENT`；
- 追加可选字段只更新 `NEXCORE_CONFIG_V1_SIZE`；
- 字段增删/重排升 `NEXCORE_ABI_VERSION`。

问题：**兼容是单向的**。

- 新调用方（更大 `struct_size`）+ 旧库：旧库只读自己认识的前缀，尾部忽略 → 可用。
- 旧调用方（更小 `struct_size`）+ 新库：新库把 `V1_SIZE` 调大后，旧调用方的 `struct_size` 小于新 `V1_SIZE` → 返回 `NX_E_INVALID_ARGUMENT`。

所以「追加可选字段只更新 `V1_SIZE`」对旧调用方是**破坏性变更**，与「向前兼容」的通常含义相反。文档没有区分 forward（新调用方→旧库）与 backward（旧调用方→新库）。**P1（方案缺陷 + 文档表述）**。

建议二选一：

- 引入 `NEXCORE_CONFIG_MIN_SIZE`（v1 初始布局）与 `NEXCORE_CONFIG_CURRENT_SIZE`；库接受 `>= MIN`，只读 `<= min(struct_size, CURRENT)` 的字段；追加字段不改变 `MIN`；
- 或者明确写「追加字段会拒绝旧调用方；v1 不承诺 backward 兼容」，并把 D-18 的「向前兼容」改成「新调用方向后兼容」。

### 7.2 `now_ms` / `clock_user` 在 Swift/Kotlin 侧怎么表达

- Swift：可以传一个 `@convention(c)` 的全局函数指针 + `UnsafeMutableRawPointer` 作为 `clock_user`；这层没有文档，但可行。
- Kotlin/JVM：**不能**直接把 Kotlin 函数变成 C 函数指针。需要：
  - 生成一个 C trampoline（每个时钟一个 `int64_t nx_clock_<name>(void *user)`），JNI 侧把 Java 对象注册到 C 表；
  - 或者 ABI 改成 `nexcore_set_clock(handle, clock_id)`，由 Bridge 调平台注册的固定入口。
- 文档只写了 C 结构体字段，没有写生成绑定如何表达，也没有写：
  - `now_ms` 会在哪些线程被调用（JS 线程、drain 线程、shutdown 路径）；
  - 是否要求线程安全；
  - 返回值的单调性要求（回拨会怎样）。
- **P1（方案缺陷 + 文档表述）**。

---

## 8. 其他 P1/P2

### P1-1（文档表述）：`#ct-raw` 的「未实测」标注不准确

design §3.4 L113 写「本轮未实测，不满足『有实测依据才能列为候选』的门槛」。但：

- `reviews/ct-capability-audit.md` L229 已记录「`#ct-raw` 也能注入上述声明……调用 raw 里的函数则正常」；
- 本轮 `/tmp/ctm6` 实测：命名空间单元写 `#ct-raw extern int getpid(void); #ct-endraw`，裸调用 `getpid()`，`ct build` exit 0，`cc -Wall -Wextra -Werror -fsyntax-only` exit 0。

所以它不是「未实测」，而是「已实测可用，但因把 C 符号带入 CT、破坏无魔法而不采用」。后者是正当理由，前者不是。建议改成「已实测可用于声明与调用；因 R5 无魔法约束不采用」。

### P1-2（文档表述）：关闭预算的调用方语义缺失

见 §3.3。

### P1-3（文档表述）：`DROPPED_SHUTDOWN` 计数口径

见 §3.2。

### P1-4（文档表述）：ACC-C-11 的「500 ms + 200 ms」

200 ms 是什么？是 `shutdown` 的额外宽限、还是测试允许的调度抖动？与 `shutdown_budget_ms=500` 的关系没有写。建议改成「`shutdown` 在 `shutdown_budget_ms` 后 ≤ 200 ms 返回」，并说明 200 ms 是调度余量。

### P2-1：ACC-C-03 与 200 ms 硬窗口的张力

async §5.2 说 continuation turn 不重置 `hard_deadline`，整个泵送 episode 上限 200 ms。ACC-C-03 要求 10k continuation 全部执行。慢设备上 10k 个 job 可能超过 200 ms，此时按 D-05 应判 `E_JS_RUNAWAY`，验收却要求全部成功。建议给该用例一个显式的测试预算（例如 `hard_deadline = 5 s`），或把 10k 降到能在 200 ms 内完成的规模。

### P2-2：ACC-A-04 的「严格相等实现必须失败」

建议改成固定谓词：「构造一个只接受 `struct_size == NEXCORE_CONFIG_V1_SIZE` 的实现，喂 `V1_SIZE + 8`，必须返回 `NX_E_INVALID_ARGUMENT`；当前实现必须返回 0」。

### P2-3：ACC-A-11 的「JS 线程已回收」

没有 ABI 可查询线程状态。建议改成「`shutdown` 返回后 100 ms 内进程线程数回到 create 前；ASAN/LSAN 无报告；再次 create 成功」。

### P2-4：ACC-A-08 的时序

建议明确「线程 1 进入 `drain(-1)` 并阻塞 ≥ 50 ms 后，线程 2 再调用」，用 barrier 或 `nexcore_stat` 的 pending 计数固定顺序。

### P2-5：p50/p95 插值

只定义了 p99。建议统一为「所有分位数使用线性插值，样本按升序，索引 `(n-1)*q`」。

---

## 9. 结论

### 9.1 本轮确实做对的部分

- 关闭顺序已收敛到 async §7.1 单一权威；design §7 与 protocol §12.6 只引用（protocol 有一句残留行为描述，见 P1-2）。
- 四类在途对象都有收尾动作；计数口径可推出 300（但需要跨三节）。
- 编号迁移彻底：只剩三处历史说明，不算残留。
- 验收从 51 条扩到 57 条，计数正确；抽检 15 条中 7 条完全可判定、7 条条件可判定、1 条不可判定。
- `E_NOT_SUBSCRIBED` 已删除且数值不复用；`$cancel` 方向已修正。

### 9.2 必须处理

1. **D-15 重开**：`.m.ct @map` 已实测可用（两个命名空间单元、双向、`cc -Werror`、运行通过）。「唯一可行」撤回；至少改为 (b)/(b') 并列，并补实测附录。
2. **P0-1**：`js_done` 不能替代 `thread_join`；`free` 必须断言 JS 线程已 join；DEAD 单点设置。
3. **P0-2**：拆分 `stop_requested` 与 `abort_requested`；关闭 turn 豁免 interrupt；或明确 JS 内部 shutdown 走 FATAL。
4. **P0-3**：若保留 (b)，补齐转发接口/生命周期/存储/初始化顺序/生成器规则；否则随 (b) 撤回。
5. **P1-1..P1-4**：修正 `#ct-raw` 标注、关闭预算的调用方语义、`DROPPED_SHUTDOWN` 计数、ACC-C-11 的 200 ms。
6. **P1-5..P1-8**：固化 bench 环境与离群值策略；区分 config 的 forward/backward 兼容；补 Swift/Kotlin 时钟注入。

### 9.3 一句话

这一轮不是「整改没做」，而是「整改把一个错误的排除前提当成了决策依据」：`.m.ct` 路线实测成立，D-15 的根转发不是唯一解；同时关闭路径还有两个会在真机上触发的竞态。修完 P0-1/P0-2 与 D-15 的重审，才谈得上通过。

---

## 附录 A：实验命令与输出

### A.1 `.m.ct` 双向命名空间调用（/tmp/ctmap）

```text
$ ct build --emit-c --ns-root . -I . -I a -I b main.ct -o out
build_exit=0
cc -std=c99 out/main.c out/a.c out/b.c -o a.out

$ cc -std=c99 -Wall -Wextra -Werror -I out out/main.c out/a.c out/b.c -o out/app
cc_exit=0

$ ./out/app
run_exit=0
```

生成的关键行：

```c
/* out/a.c */
#include "b.h"
#include "a.h"
...
int a_afn(int x) {
    return (b_bfn((x)) + 1);
}
```

```c
/* out/b.c */
#include "a.h"
#include "b.h"
...
int b_bfn2(int x) {
    return a_abase((x));
}
```

### A.2 `#ct-raw` 在命名空间单元中的调用（/tmp/ctm6）

```text
$ ct build --emit-c --ns-root . app2/app2.h.ct -o out
build_exit=0
$ cc -std=c99 -Wall -Wextra -Werror -fsyntax-only out/app2.c
cc_exit=0
```

`out/app2.c` 含：

```c
#include <unistd.h>
extern int getpid(void);
...
int p = getpid();
```

### A.3 环境

- CT：`ct v0.19.1`（`c27da3d`）
- cc：Apple clang（`cc`），`-std=c99 -Wall -Wextra -Werror`
- 实验目录：`/tmp/ctmap`、`/tmp/ctm6`；未写入仓库
