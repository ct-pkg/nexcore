# Spec：docs v0.3 整改复核（第二轮）

## 1. 目标与边界

- 目标：复核需求角色对上一轮 5 个 P0 的整改，产出 `reviews/docs-design-review-round2.md`。
- 只读：`docs/`、`/Users/samxx/vscspace/ct` 不得修改。
- 只写：`reviews/`；实验产物仅写 `/tmp`。
- 分级：P0/P1/P2，并区分「方案缺陷」与「文档表述问题」。
- 不凑条目：没有实证的问题不写；本轮若无 P0，直接写「不通过项为零」。

## 2. 已完成的取证（可直接引用）

### 2.1 多单元拓扑

- [design.md](../../../docs/design.md) §3.4 L81-L114：
  - L99 根单元唯一 `import`，调用 `js_host::start(...)`、`queue::push(...)`。
  - L100 子单元只能调用本单元函数，互不调用。
  - L101 跨单元由根提供 `nexcore_forward_*`，并向 A **注入回调/函数指针**；A 不知道 B 的名字。
  - L102 根与子单元只能经参数/函数指针/不透明句柄交互，**不通过全局变量**。
  - L106-L108 代价只写「耦合中心、根文件更长、多一层转发」。
  - L113-L114 `#ct-raw` 与 `.m.ct @map` 均标「本轮未实测」，后者理由为「只解决符号映射，不解决 include 与限定名解析」。
- [codegen.md](../../../docs/codegen.md) GEN-17 L517、SYM-04 L543、GEN-16 L516、GEN-19 L519：
  - 只规定「跨单元调用只写在根单元」「命名空间单元禁止生成跨单元调用」「生成 C 只用 cname」。
  - 全库 `nexcore_forward` 仅出现在 design L91/L101，codegen 无任何生成转发函数或注入代码的规则。
- 验收 [acceptance.md](../../../docs/acceptance.md) ACC-E-08 L89 要求「根单元经转发调用命名空间单元（正例）」，但文档没有定义该正例的输入/产物。

### 2.2 `.m.ct` 实测（本轮，/tmp/ctm5）

构造：`app/app.h.ct`（`namespace app;`）`import libc::libc;`，调用 `libc::Env.get("HOME")`；`libc/libc.m.ct` 的 prologue 为 `#include <stdlib.h>`，并有 `map trait Env for namespace libc { get(name) -> getenv(name); }`。

结果：

```text
$ ct build --emit-c --ns-root . -I . -I app main.ct -o out
build_exit=0
$ cc -std=c99 -Wall -Wextra -Werror -fsyntax-only -I out out/app.c
cc_exit=0
```

生成的 `out/app.c` 顶部出现 `#include <stdlib.h>`，调用点展开为 `getenv(("HOME"))`。结论：`.m.ct` 的 prologue **解决 include**，`map` **解决限定调用**；design L114 的排除理由与 CT 语言规范 §4.10（L543：prologue 展开进消费者 `.c` 顶部）及本实测矛盾。

### 2.3 `#ct-raw` 实测（本轮，/tmp/ctm6）

构造：`app2/app2.h.ct`（`namespace app2;`）写 `#ct-raw extern int getpid(void); #ct-endraw`，随后裸调用 `getpid()`。

结果：

```text
$ ct build --emit-c --ns-root . app2/app2.h.ct -o out
build_exit=0
$ cc -std=c99 -Wall -Wextra -Werror -fsyntax-only out/app2.c
cc_exit=0
```

生成的 `app2.c` 含 `extern int getpid(void);` 与原样调用。结论：`#ct-raw` 可注入声明并完成跨单元 C 调用；design L113 称「本轮未实测」与 `reviews/ct-capability-audit.md` L229（「调用 raw 里的函数则正常」）不一致。

### 2.4 关闭顺序

- [async-threading.md](../../../docs/async-threading.md) §7.1 L??（本轮读取段落）：7 步顺序、四类在途对象表、步骤 3.4 预算耗尽、步骤 4 `js_done` 规则、步骤 5 DEAD。
- [design.md](../../../docs/design.md) §7 L410-L427：只引用 async §7.1。
- [protocol.md](../../../docs/protocol.md) §12.6 L331-L340：只引用，但 L337 写「随后泵空，.catch 有机会执行」，未写预算耗尽时剩余 `.catch` 不执行。
- [design.md](../../../docs/design.md) §6.4 L403-L404：`NX_STAT_DROPPED_SHUTDOWN` 定义为「shutdown 时丢弃的 inbox/outbox 项数」；`NX_STAT_CLOSE_BUDGET_EXCEEDED` 为「关闭预算耗尽的次数」。

### 2.5 编号与验收计数

- 全库旧前缀仅剩：`README.md` L52/L66（历史说明）、`codegen.md` L547（删除说明）。无跨文档裸 `G-`/`R-`/`F-`/`D-` 引用（`D-xx` 为现行决策编号）。
- `grep -oE 'ACC-[A-H]-[0-9]{2}' acceptance.md | sort -u | wc -l` = **57**。
- `E_NOT_SUBSCRIBED` 已删除，protocol L118/L157 明确数值不复用。

### 2.6 bench-v1 与 config

- D-17 L325-L337：并发度 1、10k warmup + 100k 采样、p50/p95/p99/max、32 并发吞吐、含/不含 handler、版本化、设备由 Q-16 固化。
- acceptance §10 L122-L128：Release、1 KiB JSON、固定种子、p99 线性插值、计时边界；设备型号仍「待定」。
- Q-16 状态仍 `OPEN`（open-questions L30）。
- D-18 L339-L349：`struct_size >= NEXCORE_CONFIG_V1_SIZE` 接受、尾忽略；`reserved==0`；追加字段只更新 `V1_SIZE`。
- design §6.2 L337-L353：`now_ms(void *user)` + `clock_user`；L367 只写「测试注入假时钟」。
- 全库无 Swift/Kotlin 侧如何表达 `now_ms`/`clock_user` 的段落。

## 3. 待写入报告的判定（decision-complete）

### P0（4 项，均为方案缺陷）

1. **D-15 的排除前提不成立**：`.m.ct` 被以错误理由排除；实测证明 prologue 解决 include、map 解决限定调用。至少应作为实测候选重新评估，或给出实测失败证据。位置：design L114、decisions D-15 L287-L305。
2. **`js_done` 等待语义有 UAF/双 DEAD 竞态**：async §7.1 步骤 3.7 置 `js_done`，步骤 4 非 JS 调用方「`thread_join` 或等 `js_done`」，步骤 5 置 DEAD；`free` 只检查 `DEAD && drain_active==0`。若走 `js_done` 分支，JS 线程可能尚未退出，`free` 可先销毁句柄；DEAD 也可能被两方写。修法：非 JS 路径只允许 `thread_join`；`js_done` 仅作唤醒；`free` 断言线程已 join；DEAD 单点设置。
3. **JS 线程内部 shutdown 与 `stopping` 中断冲突**：async §5.2 说 interrupt 在 `stopping` 时返回非 0，§7.1 步骤 4 说 JS 内部发起立即返回并继续关闭 turn。前者会把当前脚本判为 `E_JS_RUNAWAY` 并走 FATAL，后者要求优雅关闭，二者不能同时成立；`.catch` 是否执行因此不确定。修法：分离 `stopping_requested` 与 `interrupt_abort`，关闭 turn 显式豁免，或规定 JS 内部发起一律走 FATAL。
4. **选择 (b) 的落地契约缺失**：转发函数签名、注入的接口结构、注入时机、存储位置（L102 禁止全局变量，但 A 需跨调用保存指针）、子单元初始化顺序、跨单元 cname 冲突规则均未定义；GEN-17/SYM-04 只「禁止」不「生成」，ACC-E-08 的正例没有可判定产物。`D-15` 的代价应补「业务编排上移到根单元」「每个跨单元回调一个根单元本地 wrapper（因 GAP-05 不能取 `ns::f` 地址）」。

### P1（7 项）

1. **`#ct-raw` 标注不准确**：应写「已实测可用于声明与调用；因破坏无魔法不采用」，而非「本轮未实测」。
2. **关闭预算的调用方语义缺失**：protocol §12.6 L337 无条件说 `.catch` 有机会执行，与 async §7.1 步骤 3.4「剩余 reaction 不执行」冲突；对外契约未说明「预算耗尽时部分 `.catch` 永不执行」。
3. **`DROPPED_SHUTDOWN` 计数口径不完整**：§7.1 表 response 行未写计数、inbox 行只写「累加计数」；ACC-C-05 的 `==300` 需靠 §7.2 与 §6.4 跨节推断。
4. **ACC-C-11 的「500 ms + 200 ms」无定义**：200 ms 是什么、与 `shutdown_budget_ms=500` 的关系未写。
5. **bench-v1 跨机不可比**：Q-16 未固化；无离群值处理、重复次数、热状态、macOS CPU 频率策略；payload 种子值未写；吞吐采样数未定；「不含 handler」如何构造未定义；只定义 p99 插值，p50/p95 未定义。
6. **NexCoreConfig 兼容判据单向且自相矛盾**：追加字段只更新 `V1_SIZE` 会让 `struct_size` 较小的旧调用方被新库拒绝；「向前兼容」未区分新调用方→旧库与旧调用方→新库。
7. **`now_ms`/`clock_user` 的平台表达缺失**：Swift 可用 `@convention(c)`，JVM 不能直接提供 C 函数指针，需要 JNI trampoline 或 C 侧分发表；文档未定义生成绑定形态、调用线程与线程安全。

### P2（5 项）

1. ACC-C-03 的 10k 链与 200 ms 硬窗口可能冲突：continuation turn 不重置 `hard_deadline`，慢设备上 10k 链可能被判 `E_JS_RUNAWAY`；验收未给该用例的预算豁免。
2. ACC-A-04「严格相等实现必须失败」措辞含混（最小长度语义下应写明是「回归负例」）。
3. ACC-A-11「JS 线程已回收」无可观测判据（无 ABI 查询），只能靠线程计数/ASAN 间接判。
4. ACC-A-08 需要 barrier 固定「第一个 drain 已阻塞」的时序，否则第二个可能成功。
5. 历史旧前缀（README L52/L66、codegen L547）属删除说明，不算迁移残留，报告中明确说明。

## 4. 报告结构（固定）

`reviews/docs-design-review-round2.md`：

1. 结论与分级计数（P0/P1/P2，方案缺陷/文档表述分列）。
2. P0-1 选择 (b) 可行性：逐条回答用户 5 问（A→B 路径、根单元是否变业务聚集地、函数指针接口/生命周期/循环依赖、include/符号/初始化顺序、生成器形态 + 最小产物示意）。
3. 「未实测」标注的诚实性：`#ct-raw`、`.m.ct` 的实测证据与结论。
4. 关闭顺序：4 个子问题逐条判定。
5. 编号迁移：全库搜索结果与「不算残留」说明。
6. 57 条验收抽检：15 条逐条判定表（A-04/A-08/A-09/A-10/A-11/B-03/B-07/C-03/C-05/C-11/D-03/E-04/E-08/G-05/H-05）。
7. bench-v1 可复现性。
8. NexCoreConfig 三个洞。
9. 附录：/tmp 实验命令与关键输出（ctm5、ctm6）。

## 5. 验收标准（报告自身）

- 用户 8 组问题全部有明确结论，不遗漏。
- 每条 P0/P1 指到具体文件与行号，并给出证据或实测命令。
- 15 条 ACC 每条给出「可判定/条件可判定/不可判定」及理由。
- 不出现「运行正常」「符合预期」这类不可判定措辞作为判定依据。
- 写盘仅限 `reviews/docs-design-review-round2.md`；不修改 `docs/`。
- 最终计数与 `grep` 结果一致（57 条、旧前缀仅历史说明）。

## 6. 需要用户确认

- 是否接受本轮的 P0 判定（尤其 P0-2/P0-3 属实现层竞态，不是纯文档问题）。
- 是否同意 `.m.ct` 路线需要补一次端到端实测后再决定是否维持 D-15。
