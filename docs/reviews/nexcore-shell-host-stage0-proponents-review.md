# 主张席评审：Shell 宿主「阶段 0」的最小必要集与交付物形态

> 日期：2026-11-15 ｜ 立场：**主张席**（论证四件是最小必要集、每件形态是当前最优）
> 基线：`HEAD = 9059e33`（评审期间仓库从 `249b178` 前进到 `9059e33`：T1/T2/T3 已落地；本文件定稿时 T4 已提交、T5 在途）。本文所有行号以 `9059e33` 为准。
> 在途：`tools/check.sh` 正在被并行追加门禁（T5），行号会继续漂移；因此凡引用 `check.sh` 处，正文同时给出**断言原文**，用 `grep -n "导出 T 符号应为" tools/check.sh` 之类定位即可。**本文件不引用在途改动，也不跑门禁**（§1.3）。
> 对象：[裁定](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md)、[PH-10 卡](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md)、[修订清单](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-proposal-revisions.md)。
> 纪律：只读仓库 + 只写本文件；未改 `src/`、`tools/`、卡片、`abi/` golden、门禁；未 push、未打 tag。
> 说明：本文件不复述裁定结论，只给判据；每条判断后面都跟文件行号或一段我自己跑出来的输出（附录 A）。

---

## 0. 结论先写

| # | 命题 | 判定 | 判据（一句话） |
| --- | --- | --- | --- |
| 1 | 四件即最小必要集 | **成立，但要分档**：D1/D2/D3 是阻塞型，D4 是闭合型 | 删 D1/D2/D3 各有一个可复现的「交付失败」；删 D4 不阻塞 PH-11，只留一条假绿通道（§2.4） |
| 1′ | 没有第五件 | **成立** | 9 个候选逐条出局，统一判据是「是否触碰冻结面/门禁面」（§2.5） |
| 2 | 泵复用 `nexcore_drain`、不新增导出符号 | **成立，且是唯一合法解** | 新增导出会同时红 ⑩ 与 ACC-A-02；泵放在 `eval` 内把「必须做」变成不可遗忘（§3） |
| 2′ | 该形态是否已真被门禁覆盖 | **部分覆盖**：`pre-pump` 有判据，`post-pump` 无判据 | 可核对推理见 §3.4；这是本轮发现的第一个缺口 |
| 3 | 尾部 4 标量、不升 `abi_version` | **成立，但「零影响」有前提** | 布局实测 MIN 不动 72、新字段 88/96/104/112；零影响只在**头库同源**时成立，跨版本混用会静默失效——这条风险正好由 D3 的 `.a` 消掉（§4.5） |
| 4 | 四项归属库层而非 shell 宿主 | **半成立**：四项都不归宿主成立；但只有 D1/D2 属库层，D3 属构建层、D4 属门禁层 | 卡片 §1.1 自己的归属表就是这么分的（[card.md:21-28](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L21-L28)），命题措辞需要收窄（§5.1） |
| 4′ | 移动端同样受益 | **方向成立，但两件不同级**：D1 结构上直接受益；D2 要等 bindings 才被移动端真正消费 | §5.2 / §5.4 |

---

## 1. 基线与方法

### 1.1 我实际核了什么

| 类别 | 对象 | 用途 |
| --- | --- | --- |
| 读 | `src/NexCore.h.ct`、`src/NexCore.ct`、`src/js_backend/*`、`src/units.json` | 冻结面与调用通路（§3/§4） |
| 读 | `tools/check.sh`（⑩ / ⑤c / ⑮）、`tools/check-boundaries.sh` | 门禁断言的具体强度（§2.2/§3） |
| 读 | `docs/acceptance.md`（ACC-A-01/02、ACC-C-03）、`docs/decisions.md`（D-18）、`docs/roadmap.md:56` | 冻结判据的出处 |
| 读 | `tests/pump_microtask.c`、`tests/native_provider.c`、`tests/config_negotiate.c`、`abi/abi-layout.txt`、`abi/expected-forward-decls.txt` | 已有夹具的覆盖边界 |
| 跑 | 5 项（附录 A）：边界盲区、ABI 布局算术、`struct_size` 协商、泵前红证、dyld 可搬运性 | 把「风险」换成可复现输出 |

### 1.2 为什么不用裁定文档的实测顶替

裁定文档的实测是**泵前**快照（`payload=0`、无 `.a`、无 ⑮）。T1/T2/T3 已落地，其中 D1、D2 的代码事实变了，D3/D4 没变。本文对 D1/D2 用**落地后代码**重新核（§3/§4），对 D3/D4 用我自己跑的输出重新核（A1/A5），不引用旧结论。

### 1.3 没跑全量门禁的理由（必须写明，否则本文件结论不可复核）

另一会话正在同一工作树推进 T4（`tools/check.sh` 处于 `M` 状态）。用户纪律要求门禁不得并发运行；且我若此时跑 `check.sh`，输出会混入未提交改动，无法作为 `9059e33` 的判据。因此本文所有「实测」都在 `/tmp` 下的独立副本上完成，命令与输出见附录 A。

---

## 2. 命题一：四件就是最小必要集

### 2.1 先把「必要」定义成可操作的东西

空谈「必要」会滑向「有用」。我用的判据是**删除测试**，并且把「做不成」解释成**做出来也无法判定/无法交付**，而不是「写不出代码」——否则几乎没有任何一件是必要的（宿主可以从零手搓 QuickJS 绑定，只是不叫交付）：

- **M1（阻塞）**：删掉该项后，PH-11 至少有一个交付物**无法判定对错**（不是质量下降、不是体验变差）。
- **M2（闭合）**：删掉该项后，仓库里会留下一个**不可用一条命令核对的空洞**，且补它的改动必须落在**另一个归属**（编码侧门禁 / 构建脚本）上。
- **M3（不可下放）**：若不在阶段 0 做，PH-11 想补它就必须重开一个**冻结面**（`ACC-A-01/02`、`NEXCORE_ABI_VERSION`、产物形态、扫描面）。

一件进阶段 0，当且仅当满足 M1 或 M2，且满足 M3（不满足 M3 的属于「能力」，随使用它的阶段做）。下面逐件对。

### 2.2 D1 微任务泵 —— M1 ✓ / M3 ✓

**实测（附录 A4，我自己跑的红证）**：用泵前快照的库跑 `tests/pump_microtask.c`：

```
create rc=0
A: send+drain rc=0 hits=0
FAIL: microtasks not pumped (expect hits=1)
rc=3
```

**具体失败场景（不是抽象风险）**：`nex job.nexjs` 内容是 `Promise.resolve().then(function(){ hit=1; });` 后续再读 `hit` —— 退出码 0、stdout 空、宿主拿到的 payload 是 `0`。宿主无法区分「脚本没写这句」和「引擎没跑这句」，PH-11 的「脚本异常 → 合理 exit code」判据在这个状态下**不可判定**：所有失败都长得像成功。

**M3 的依据**：泵的位置决定是否需要新导出。公开 ABI 只有 7 个函数（[NexCore.h.ct:127-139](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L127-L139)），实现体里引擎句柄是不透明 `void *js`（[NexCore.ct:54](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L54)），宿主拿不到 `JSContext`（裁定 §2.1 已实测 `js_std_loop` 链接得上但调不了）。所以「泵放哪里」这个决定**在阶段 0 做和在 PH-11 做，成本不一样**：在 PH-11 做，等于让一个「新建宿主」的 phase 顺带改求值语义，一旦回滚就连带宿主不可用。先做，PH-11 的失败才可归因。

### 2.3 D2 Provider 注册 —— M1 ✓ / M3 ✓

**结构性证据**：7 个 ABI 函数里没有任何「注册/绑定」入口（[NexCore.h.ct:127-139](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L127-L139)）；`src/` 里 `js_std_add_helpers` / `js_std_loop` 命中数为 **0**（我跑的：`grep -rn "js_std_add_helpers\|js_std_loop" src/ | wc -l` → `0`），所以连 `console` 都不存在，JS 侧没有**任何**通往 C 的路径。

**具体失败场景**：PH-11 的交付物「`Shell.print`」如果只有 sink，脚本只能「返回一个值」，多行调试输出不可表达——`Shell.print('a'); Shell.print('b')` 在表达式模式下连语法都过不了（源码被包成 `(src)`，[js_backend_quickjs.ct:204-212](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L204-L212)），即便到了程序模式，也只能靠返回数组、让宿主自己拆。这不是「打印体验差」，是**宿主原生能力的注入通道根本不存在**，PH-11 会退化成「把字符串拼回来」。

**M3 的依据**：注册入口只有两种落法，两种都撞冻结面——
1. 新增导出函数 → 红 [check.sh:468](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L468)（导出 `T` 符号必须 7 个）与 [acceptance.md:64](file:///Users/samxx/trae-ws/nexcore/docs/acceptance.md#L64)；
2. 新增跨 ABI 结构体（vtable）→ 红 [acceptance.md:63](file:///Users/samxx/trae-ws/nexcore/docs/acceptance.md#L63)（发布头里 struct 名只允许 `NexCoreConfig`）与 [expected-forward-decls.txt](file:///Users/samxx/trae-ws/nexcore/abi/expected-forward-decls.txt#L1)（只有一行）。

卡片选的是第三条路（尾部字段），正因为前两条走不通——这正好说明它必须在阶段 0 **先裁定**，不能留到 PH-11 边写边试。

### 2.4 D3 `.a` 与 D4 扫描面 —— 一个是 M1，一个是 M2

**D3：M1 ✓（有实测）**。附录 A5 是我跑出来的：

```
$ cd /tmp && ./cfgneg
dyld: Library not loaded: build/bin/libnexcore.dylib
  Reason: tried: 'build/bin/libnexcore.dylib' (no such file), ...
rc=134
$ cd /tmp/nxhost && ./cfgneg
struct_size = MIN            rc=0 ...
CONFIG NEGOTIATION OK
rc=0
```

`otool -D` 显示该 dylib 的 install_name 就是**相对路径** `build/bin/libnexcore.dylib`（`check.sh:454-456` 没有 `-install_name`）。也就是说：今天任何宿主二进制的可运行性绑定在「从构建目录启动」上。裁定 §7.3 要求把 `nex` 装到 `~/.local/bin`，这正是**一定失败**的用法。具体场景不是「分发不便」，是「拷走即崩，rc=134」。

**D4：M2 ✓ / M1 ✗（必须说清）**。附录 A1：

```
$ bash tools/check-boundaries.sh
boundaries: PASS（19 个文件，无 #if / 无 errno / import 顺序正确）      # rc=0
$ bash tools/check-boundaries.sh /tmp/blind/hosts
/tmp/blind/hosts/shell/demo.ct:1: 禁止条件编译：#if 1
boundaries: FAIL（1 处违规，扫描 1 个文件）                              # rc=1
```

默认扫描面写死在两处、且**互相覆盖**：[check-boundaries.sh:16-18](file:///Users/samxx/trae-ws/nexcore/tools/check-boundaries.sh#L16-L18) 是默认值，[check.sh:401](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L401) 又显式传了两个目录——只改一处，另一处照样让新目录成为盲区。

但必须诚实：**C 宿主下 `hosts/` 没有 `.ct`，所以这条不阻塞 PH-11**。它的必要性来自另一个方向——[roadmap.md:56](file:///Users/samxx/trae-ws/nexcore/docs/roadmap.md#L56) 写的是「**所有 `.ct`** 通过 `check-boundaries.sh`」，这是一条**仓库级断言**，而它的可核对性依赖扫描面等于「可能出现 `.ct` 的目录集合」。一旦 `hosts/` 出现第一个 `.ct`（裁定 §5 保留的 B 方案随时可能复活），这句话就变成假绿，而补扫描面的改动落在 `tools/`（编码侧唯一维护），宿主作者**无权自行完成**。所以它满足 M2 与 M3，不满足 M1。

**结论（分档）**：严格的最小必要集是 **D1+D2+D3**；D4 是「同批闭合」，理由是边际成本两行、且跨归属补做会卡住下一个人。我拒绝把 D4 说成阻塞项——那是拿「将来可能」当判据。

### 2.5 为什么没有第五件：候选逐个出局

统一判决线就是 §2.1 的 M3：**是否触碰冻结面/门禁面**。不触碰的一律下放。

| 候选 | 触碰哪条冻结面 | 出局理由 | 应归属 |
| --- | --- | --- | --- |
| 导出 `nexcore_pump` | ⑩ 计数 7 / ACC-A-02 | 直接红 [check.sh:468](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L468) | 不做（非法），见 §3 |
| 独立 `nexcore_register_native()` | 同上 | 同上 | 不做，见 §4.2 |
| `NexCoreNativeTable` 结构体 | ACC-A-01 + 布局 golden | 发布头 struct 名被 [acceptance.md:63](file:///Users/samxx/trae-ws/nexcore/docs/acceptance.md#L63) 锁死 | 不做 |
| `flags` 位 / 复用 `reserved` | 老调用方语义 | 两者都是**硬拒绝**（[NexCore.ct:78-79](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L78-L79)），改了就是放宽对旧调用方的校验 | 不做，见 §4.1 |
| 程序模式求值（去括号） | 不触碰 | 纯 `js_backend` 内部，宿主无感 | **PH-11**（[card.md:207](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L207)） |
| 异常文案 / 结构化错误面 | 不触碰 | 同上；且不影响「能不能搭接缝」，只影响「跑得好不好」 | **PH-11** |
| D-05 预算（10k 让出 / 200ms / `E_JS_RUNAWAY`） | 不触碰 ABI，但需要 worker/turn 模型 | 阶段 0 没有宿主消费者；且它与「泵到空」是两个语义（§3.6） | P2 |
| `Shell.spawn` / 沙箱 / 信号 / 字节码 | 不触碰（宿主侧） | 阶段 0 明确不写宿主 C 代码（[card.md:174](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L174)） | PH-11+ |
| `tools/check-host.sh` | 门禁面 | 阶段 0 没有宿主代码，脚本只能是空壳，反而制造「有门禁」的错觉 | PH-11 |

**这张表就是「多一件即过度设计」的判据**：任何一件要么撞冻结面（做不了），要么不撞但当下没有消费者（做了也是空转）。

---

## 3. 命题二：复用 `nexcore_drain`、不新增导出符号

### 3.1 冻结面是硬的，有三处断言

| 断言 | 位置 | 强度 |
| --- | --- | --- |
| 导出 `T` 符号必须 7 个 | [check.sh:468](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L468) | 计数 ≠7 直接 exit 1 |
| 非 `nexcore_*` 前缀即失败 | [check.sh:472](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L472) | 前缀白名单 |
| 白名单本体固定 7 行 | [check.sh:445-452](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L445-L452) | `-exported_symbols_list` 生效面 |
| 验收条款 | [acceptance.md:64](file:///Users/samxx/trae-ws/nexcore/docs/acceptance.md#L64) | 「只出现文档列出的 7 个」 |

也就是说「新增一个 pump 导出」不是风格问题，是**会红**。这一点上卡片没有选择空间，只有一条合法路径。

### 3.2 为什么「泵进 eval」优于「泵做 API」

判据不是「少一个符号」，而是**失败模式**：

- 若 `nexcore_pump` 是独立 API，宿主必须**记得**在 `send` 之后、读结果之前调用它。漏调时的现象与今天完全一致：`hits=0`、rc=0、无报错（附录 A4）。这就是「把必须做变成记得做」。
- 泵进 `eval` 之后，宿主只要用 ABI 就自动正确：`drain` → `pump_inbox`（[NexCore.ct:259](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L259)）→ `Backend.eval`（[NexCore.ct:203](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L203)）→ eval 内两处 `pump(h, 0)`（[js_backend_quickjs.ct:192](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L192)、[:239](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L239)，见比对说明）。调用点只有一个，漏不掉。
- 这条形态还顺带满足 ACC-C-03 的**顺序**条款：「下一条 native 消息在链完成后才被处理」——因为 eval 之后泵空，才会返回给 `pump_inbox` 推 outbox（[NexCore.ct:203-209](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L203-L209)）。若泵是外部 API，这条顺序就变成**调用方义务**，验收条款也就无法由库保证。

> 行号比对说明：`js_backend_quickjs.ct` 在 T1 时为 138 行（`post-pump`），T3 插入 provider 代码后移到 239 行。两处 `(void)pump(h, 0)` 的语义未变。

### 3.3 夹具证明「不新增符号也能满足 E1」

[tests/pump_microtask.c:37-39](file:///Users/samxx/trae-ws/nexcore/tests/pump_microtask.c#L37-L39) 只调 `nexcore_send` + `nexcore_drain`，全文不出现任何非 ABI 符号；我另外核了一条实测：产出的发布头里 `pump` 命中数为 **0**（`grep -c pump /tmp/nxhost/build/abi/NexCore.h` → `0`）。**泵在 ABI 面完全不可见**，这是「不新增导出符号」最硬的证据。

### 3.4 缺口 A：`post-pump` 目前没有判据（本轮第一个实质发现）

推理链，全部可核对：

1. 单表达式面下，`eval` 的返回值在 `JS_JSONStringify` 之后就已定型（[js_backend_quickjs.ct:214-238](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L214-L238)），`post-pump`（:239）**改不了本条消息的返回**。
2. 夹具 A/B 都是「bootstrap 排 job → send 读变量」，job 在**读之前**由 `pre-pump`（:192）执行完毕。所以 A/B 通过靠的是 `pre-pump`。
3. 因此删掉 :239 之后，⑮（[check.sh:621-631](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L621-L631)）仍然 PASS，全仓没有第二处夹具碰 `pump`（我跑的：`grep -rln pump tests/` → 只有 `tests/pump_microtask.c`）。

**它为什么仍然必须存在**（否则是纯冗余）：`post-pump` 唯一可观测的效果是「**最后一条消息**排下的 job 在 `drain` 返回前执行」。一旦 D2 就位，这个效果就有了观测面：脚本末尾 `Promise.resolve().then(function(){ Shell.print('done') })` 在无 `post-pump` 时**永不执行**（后面没有下一条消息，`shutdown` 的 `pump_inbox` 也只在 inbox 非空时才 eval，[NexCore.ct:275-280](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L275-L280)）。

**建议（不改本卡判据，只进 PH-11 入口）**：PH-11 的第一条 provider 夹具必须包含「末尾消息 + `.then` + `Shell.print`」，用它把 :239 钉住。今天写成「E1 已覆盖 post-pump」是不成立的。

### 3.5 缺口 B：无预算的「泵到空」今天可达挂死（结构性结论，未实测）

`pump` 的循环只在 `JS_ExecutePendingJob` 返回 `0` 或 `<0` 时退出（[js_backend_quickjs.ct:274-283](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L274-L283)）。自续链（`var f=function(){return Promise.resolve().then(f)}; f();`）永不返回 0。

可达性：`bootstrap` 是 `JS_EVAL_TYPE_GLOBAL` 的**脚本模式**求值（[js_backend_quickjs.ct:157-165](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L157-L165)），init 阶段不泵；但第一条消息进入 eval 时先跑 `pre-pump`（:192），于是**第一条 `send`+`drain` 就永久挂死**。

我没有实测（当前不能建库，理由见 §1.3），因此按 `UNVERIFIED` 记：结论来自代码路径，不是运行输出。它不影响本卡成立（卡片明确不声称 runaway 判据，[card.md:75](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L75)），但必须进 PH-11 的第一项裁定：**在 D-05 预算落地前，是否允许脚本制造无界微任务链**。

### 3.6 ACC-C-03 的账要算清（不许顺手认领）

[acceptance.md:95](file:///Users/samxx/trae-ws/nexcore/docs/acceptance.md#L95) 要求「10k continuation 全执行、顺序正确、下一条消息在链后处理」，并注明「10k 配额是**让出**、不是失败」。当前同步泵空满足前两条的字面要求，但**不满足「让出」**：同步实现里没有 turn 边界，10k 链会占满一次 `drain` 调用。所以阶段 0 只能认领 ACC-C-03 的「顺序」一半，「预算/让出」一半仍是 P2 的 D-05。**谁在收口报告里写「ACC-C-03 已通过」，就是把两半混算**。

---

## 4. 命题三：尾部 4 标量、不升 `abi_version`

### 4.1 先排除「用现成空位」这条路

结构里有两个看起来能复用的位置：`flags` 与 `reserved`。两者都是**硬拒绝**：

```
NexCore.ct:78    if (cfg->reserved != 0) return NX_E_INVALID_ARGUMENT;
NexCore.ct:79    if ((cfg->flags & 0xFFFFFFFEu) != 0) return NX_E_INVALID_ARGUMENT;
```

判据：复用 `reserved` 会让所有「按约定填 0」的旧调用方语义反转；复用 `flags` 未分配位必须先删掉 :79 的拒绝——那等于对**所有**旧调用方放宽校验，且这些调用方无法感知。所以「尾部追加 + 可选语义」不是习惯，是**唯一不改变旧调用方行为的做法**。D-18 写的就是这一条（[decisions.md:370-372](file:///Users/samxx/trae-ws/nexcore/docs/decisions.md#L370-L372)）。

### 4.2 为什么是 4 个**标量**而不是一个结构体或一个函数

| 形态 | 撞什么 | 结论 |
| --- | --- | --- |
| 独立 `nexcore_register_native()` | ⑩ 7 符号（[check.sh:468](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L468)） | 不可行 |
| `NexCoreNativeTable`（vtable 结构体） | 发布头只允许一个 struct（[acceptance.md:63](file:///Users/samxx/trae-ws/nexcore/docs/acceptance.md#L63) + [forward-decls:1](file:///Users/samxx/trae-ws/nexcore/abi/expected-forward-decls.txt#L1)），且布局 golden 只认 `NexCoreConfig` | 不可行 |
| 4 个尾部标量 | 无 | **选中** |
| `const char **names` + count | 无 4 个标量以外的问题，但需要在 ABI 里表达**元素生命周期/所有权**（头里没有地方写），且调用方要维持数组存活 | 更差 |

### 4.3 布局实测（附录 A2）

```
MIN_SIZE=72 CURRENT_SIZE=88 sizeof(V2)=120
off(native_call)=88 off(native_user)=96 off(native_names)=104 off(native_names_len)=112
old caller struct_size == MIN(72) < off(native_call)=88 -> 视为未提供
```

三点可直接核对：
1. `NEXCORE_CONFIG_MIN_SIZE` **不变**（仍是 `offsetof(bootstrap_len)+sizeof(size_t)`，[NexCore.h.ct:122-124](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L122-L124)），旧调用方的门槛不动；
2. 新字段全部落在 `>= 88` 区间，`>= MIN` 的判定（[NexCore.ct:75](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L75)）完全不涉及它们；
3. 尾块无填充（88→120 严格递增 8 字节），所以「追加进 struct」与「外面套一层」得到完全相同的偏移——这也是我能在不改头的前提下做这次实测的原因。

### 4.4 「对老 `struct_size` 调用方零影响」的三层证据

| 层 | 证据 | 说明 |
| --- | --- | --- |
| 结构性 | [NexCore.ct:75](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L75) 只判 `>= MIN`；新字段读取被单独门控（`struct_size >= CURRENT_SIZE` 才读，[NexCore.ct:81-94](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L81-L94)） | 旧调用方根本进不了读新字段的分支 |
| 夹具 | [tests/native_provider.c:171-200](file:///Users/samxx/trae-ws/nexcore/tests/native_provider.c#L171-L200)：`struct_size = MIN`、尾部填 `0xA5`，断言 `typeof Shell === "undefined"` | 若越界读，`0xA5A5…` 会被当函数指针 |
| 实测 | 附录 A3（泵前库 + `tests/config_negotiate.c`）：`struct_size = MIN` → `rc=0`；`CURRENT+64` 垃圾尾 → `rc=0` | 证明协商语义在加字段前后一致 |

### 4.5 「不升 `abi_version`」的真实代价，以及它为什么被 D3 兜住（本轮第二个实质发现）

D-18 明确两个方向都成立（[decisions.md:371](file:///Users/samxx/trae-ws/nexcore/docs/decisions.md#L371)），其中「新调用方 → 旧库」这一方向的含义是：**宿主按新头编译、运行期拿到旧库 → 4 个字段被静默忽略 → `Shell` 不存在**。

具体失败场景（今天就会假绿）：`nex` 用新头编译、`dyld` 找到旧 `libnexcore.dylib` → 脚本第一行 `Shell.print('hi')` 抛 `ReferenceError` → 由于错误文案尚未透出（[js_backend_quickjs.ct:208-219](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_quickjs.ct#L208-L219) 取到异常即 free 并返回 -1），`NexCore` 只把 `last_error` 置 `NX_E_INTERNAL` 且不投递（[NexCore.ct:203-209](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L203-L209)）→ 宿主看到 `count=0`、进程 exit 0。**没有一条判据能红**。

所以命题三的成立条件不是「不升版本」，而是「**头库同源**」：
- `.a` 静态链接 → 头与库在同一次构建里绑定，混版本**不可能发生**；
- 若走 dylib + rpath（D3 的备选）→ 运行期可替换，本节的假绿就是真实概率事件。

**结论**：D2 的「不升 `abi_version`」与 D3 的「产出 `.a`」不是并列的两件事，**互为前提**。删掉 D3，D2 的正确性主张就退化成「在正确部署下正确」。这比「少一个符号」重要得多，建议写进 PH-11 卡的入口说明。

### 4.6 4 个字段能不能减到 3？

不能靠省 `native_names_len` 来省：ABI 是外部边界，长度必须由提供方给出，库不得对宿主指针做 `strlen`。这与既有 `bootstrap_js` + `bootstrap_len` 完全同构（[NexCore.h.ct:109-110](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L109-L110)）。4 个字段 = (回调, 上下文, 白名单, 白名单长度)，少任何一个都会在边界上引入「猜」。

另外，「名字用 JSON 串」这条也不贵：落地实现把白名单的解析放到了 JS 侧（`JSON.parse(__nx_native_names)`），库内没有引入任何 JSON 解析依赖——用现成引擎解析自己的白名单，成本为 0。

### 4.7 一处卡面/实现的判定阈值偏差（建议收口前统一）

- 卡面写的是：`struct_size` 小于 `offsetof(native_call)` 视为未提供（[card.md:98](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L98)）。
- 落地写的是：`struct_size >= NEXCORE_CONFIG_CURRENT_SIZE` 才读（`NexCore.ct` create 的 native 段）。

两者对 E3 覆盖的用例（`MIN` 与 `CURRENT`）**判定一致**，但对 96/104/112 这种「部分覆盖」的调用方判定不同：卡面会读，落地不读。落地更保守（全有或全无），我倾向保留实现、把卡面改成同一措辞——否则下一个 phase 会把卡面文字当规范去写夹具，写出一个与实现相反的期望。这条不影响命题三成立，但属于「交付物形态是否自洽」的范围。

---

## 5. 命题四：归属库层与移动端受益

### 5.1 「四项归库层」这句话要收窄

卡片 §1.1 的表自己就分了层（[card.md:21-28](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L21-L28)）：

| 交付物 | 实际归属 | 依据（可由实现文件反推） |
| --- | --- | --- |
| D1 泵 | **库层** | 改动落在 `src/js_backend/` |
| D2 Provider | **库层** | 改动落在 `src/NexCore.h.ct` + `src/` |
| D3 `.a` | **构建层** | 改动落在 `tools/check.sh` |
| D4 扫描面 | **门禁层** | 改动落在 `tools/check-boundaries.sh` + `tools/check.sh` |

**成立的部分**（也是命题真正想说的）：四件**没有一件**属于 shell 宿主——没有一条判据需要 `hosts/` 存在就能判定。这是本轮最该肯定的一点，因为它决定了「即使不做 shell 宿主，这四件也该做」。
**要改的部分**：把「归属库层」写成四项并列，会让 D3/D4 的评审责任落到库作者身上，而实际改动在编码侧维护的 `tools/`（[check.sh:5-12](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L5-L12) 的归属约定）。按这个措辞派活，会重演「宿主作者去改总门禁」的冲突。

### 5.2 移动端受益是结构性的，来源是「唯一通路」

公开 ABI 只有一条消息通路：`send` 入队（[NexCore.h.ct:131](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L131)）→ `drain` 排空（[NexCore.h.ct:133](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L133)）→ `pump_inbox` → `Backend.eval`（[NexCore.ct:203](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L203)）。没有第二条路径能求值。

因此：**泵只要在 eval 内，任何宿主都不可能绕过它**。反过来说，泵若放在宿主侧（无论是 CLI 自己调 `js_std_loop`，还是让 Swift/Kotlin 各写一遍「何时泵」），那么「CLI 修好了」这件事**不会传递**到移动端——这正好是 `experiments/src/bridge.ct:232` 的形态：探针里自己 `while (JS_ExecutePendingJob(...))`，永远不会进入产品。

### 5.3 反事实的具体形态

若 D1 不在阶段 0 做（按提案原文让宿主泵）：

- CLI 方案不可行：宿主拿不到 `JSContext`，`js_std_loop` 调不了（裁定 §2.1 已实测链接得上但无参可传）；
- 于是只剩「新增导出 `nexcore_pump`」一条路 → 红 ⑩（§3.1）；
- 移动端各写一遍 → 三个宿主三套泵送时机，`Promise` 语义在 CLI 与移动端不一致，而 `ACC-C-03` 是按**库**验收的，不是按宿主。

### 5.4 必须打的两处折扣（否则主张席自己先失真）

1. **仓库今天没有真正的 Swift/Kotlin 宿主。** `experiments/host/abi_host.swift` 用的是旧 ABI（第 8/12/16 行是 `nexcore_abi_add` / `nexcore_pair_make` / `nexcore_apply`，与 `send`/`drain` 无关）。所以「移动端今天 `Promise` 也是死的」是**由唯一通路推出的结构性事实**，不是实测；它的实测等价物是 CLI 侧的等价形态（附录 A4，`hits=0`、rc=3）。写成「已实测移动端死」会被下一个人当成证据。
2. **D2 的移动端收益要等 bindings。** 注册入口今天只有 CLI 会用；移动端要真受益，还需要把 `NexCoreNativeCall` 映射成 Swift/Kotlin 闭包——而仓库里目前没有任何绑定代码消费 provider（`schema/generated/` 只有 fixture 的类型映射）。所以 D2 的准确描述是「**同一个入口，移动端以后不必另开**」，不是「移动端今天就吃到」。

---

## 6. 形态最优性：逐件的备选与代价

| 交付物 | 当前形态 | 被否的备选（为什么更差） | 付出的代价（必须承认） |
| --- | --- | --- | --- |
| D1 | `js_backend::pump(h, max_jobs)` 内部方法；`eval` 前后各泵空；宿主入口 = `drain` | ①导出 `nexcore_pump`：红 ⑩（§3.1）；②在 `NexCore.ct` 里泵：该层只有不透明 `void *js`（[NexCore.ct:54](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L54)），仍要回头调 backend；③`send` 后泵：`send` 只入队不碰引擎，会把「求值点」拆成两个 | 无预算 → 自续链挂死（§3.5）；`post-pump` 暂无判据（§3.4） |
| D2 | `NexCoreConfig` 尾部 4 标量 + 单回调 + 名字白名单 | 见 §4.2 三种 | 能力只能在 `create` 期一次性声明，不能运行时新增；跨版本混用会静默失效（§4.5） |
| D3 | `ar rcs` 静态归档 + 归档面判据 | ①dylib + rpath：要改 dylib 的 `install_name`、要两文件分发、且运行期可换库（把 §4.5 的假绿变成真风险，实测 A5 的 rc=134 就是它今天的形态）；②绝对路径 dylib：不可搬 | 产物变大（quickjs 档静态包含引擎）；`.a` 被 `.gitignore` 忽略（[.gitignore:4](file:///Users/samxx/trae-ws/nexcore/.gitignore#L4)），产物路径必须在 `build/` 下，不能进版本库 |
| D4 | 脚本默认目标 + `check.sh` 实参**同时**加 `hosts` | ①只改脚本默认：`check.sh:401` 显式传参覆盖，仍盲；②只改 `check.sh`：用默认命令的人仍盲；③放进新的 `check-host.sh`：阶段 0 无宿主代码 → 空脚本 + 门禁分裂 | 只有在 `hosts/` 出现 `.ct` 时才产生实际拦截力（§2.4） |

**四件的共同形态特征**：每一件都把「裁决」和「最小实现」绑在同一次提交里，且都能用**不依赖 `hosts/` 的夹具**判定。这正是「阶段 0」该有的形状——判据先于宿主存在。

---

## 7. 由本轮判据直接推出的 PH-11 硬约束

这四条不是建议，是上面判据的推论；建议原样写进 PH-11 卡的入口条件。

| # | 约束 | 推论来源 |
| --- | --- | --- |
| C1 | 不得新增任何导出函数；若需要「有限泵」，改的是 `eval` 内的调用策略，不是 ABI | §3.1 三处断言 |
| C2 | provider 能力必须在 `create` 期一次性声明完；不得为「运行时加能力」开新入口 | §4.2 + ⑯ 三态（`NULL` / 白名单空 / 正常）已经是完整状态机，再加一维就没有判据 |
| C3 | 必须先落**错误面**再做 `Shell.*` 的负例；否则「注册失败」「脚本抛错」「库版本不匹配」三种情况在宿主侧同形（`count=0` + exit 0） | §4.5 |
| C4 | `post-pump` 必须有判据（末尾消息 + `.then` + `Shell.print`），否则删掉它全绿 | §3.4 |

---

## 8. 本轮做了什么、没做什么

**做了**：读三份文档 + 冻结面涉及的 7 个实现/门禁文件 + 3 个夹具；在 `/tmp` 独立副本上跑 5 项实测（附录 A）；核对了 T1/T2/T3 落地代码与卡面的一致性，发现 2 个实质缺口（§3.4 `post-pump` 无判据、§3.5 无预算挂死）与 1 处措辞偏差（§4.7）。

**没做**（按纪律）：未改 `src/`、`tools/`、卡片、`abi/` golden、门禁脚本；未建 `hosts/`；未提交实现；未跑全量门禁（§1.3，另一会话在途）；未 push、未打 tag。

**本轮不接受的三件事**（主张席也要划边界）：
1. 不接受「E1/E2 已覆盖泵的全部语义」——只覆盖 `pre-pump`（§3.4）；
2. 不接受「ACC-C-03 随阶段 0 关闭」——只关闭顺序一半（§3.6）；
3. 不接受把「移动端受益」写成已实测——那是结构性推论（§5.4）。

---

## 附录 A：实测记录（原始输出）

所有实测均在 `/tmp` 下的独立副本完成；工作树未改动。`/tmp/nxhost` 是**泵前快照**（无 `.git`；`grep -c pump src/js_backend/js_backend_quickjs.ct` → `0`）。

### A1 边界扫描盲区

```
$ mkdir -p /tmp/blind/hosts/shell && printf '#if 1\nint x;\n#endif\n' > /tmp/blind/hosts/shell/demo.ct
$ cd /Users/samxx/trae-ws/nexcore && bash tools/check-boundaries.sh
boundaries: PASS（19 个文件，无 #if / 无 errno / import 顺序正确）
rc_default=0
$ bash tools/check-boundaries.sh /tmp/blind/hosts
/tmp/blind/hosts/shell/demo.ct:1: 禁止条件编译：#if 1
boundaries: FAIL（1 处违规，扫描 1 个文件）
rc_explicit=1
```

结论：默认目标之外的目录不被扫描；脚本本身有能力报出违规（rc=1），盲区来自「扫描面写死」。

### A2 尾部 4 字段的布局算术

```
$ cc -std=c99 -Wall -Wextra -Werror -I /tmp/nxhost/build/abi /tmp/layout_probe.c -o /tmp/layout_probe && /tmp/layout_probe
MIN_SIZE=72 CURRENT_SIZE=88 sizeof(V2)=120
off(native_call)=88 off(native_user)=96 off(native_names)=104 off(native_names_len)=112
old caller struct_size == MIN(72) < off(native_call)=88 -> 视为未提供
rc=0
```

探针把 4 个字段套在 `struct NexCoreConfig` 外面；因尾块无填充，结论与直接追加进结构体等价。

### A3 `struct_size` 协商（泵前库，`tests/config_negotiate.c`）

```
$ cd /tmp/nxhost && /tmp/cfgneg
struct_size = MIN-1          rc=1 out=0x0
reserved != 0                rc=1 out=0x0
abi_version + 1              rc=1 out=0x0
struct_size = MIN            rc=0 out=0x7766c201c0
struct_size = CURRENT+64     rc=0 out=0x7766c201c0
struct_size = CURRENT        rc=0 out=0x7766c201c0
CONFIG NEGOTIATION OK
rc=0
```

### A4 泵前红证（`tests/pump_microtask.c` × 泵前库）

```
$ cc -std=c99 -Wall -Wextra -I /tmp/nxhost/build/abi tests/pump_microtask.c /tmp/nxhost/build/bin/libnexcore.dylib -o /tmp/pm_pre
cc_rc=0
$ cd /tmp/nxhost && /tmp/pm_pre
create rc=0
A: send+drain rc=0 hits=0
FAIL: microtasks not pumped (expect hits=1)
rc=3
```

### A5 dylib 的可搬运性（同一个二进制，只换 cwd）

```
$ cd /tmp && ./cfgneg
dyld[64247]: Library not loaded: build/bin/libnexcore.dylib
  Referenced from: <…> /private/tmp/cfgneg
  Reason: tried: 'build/bin/libnexcore.dylib' (no such file), '/System/Volumes/Preboot/Cryptexes/OSbuild/bin/libnexcore.dylib' (no such file), 'build/bin/libnexcore.dylib' (no such file)
rc=134
$ cd /tmp/nxhost && ./cfgneg
struct_size = MIN            rc=0 out=0x7766c201c0
CONFIG NEGOTIATION OK
rc=0
```

说明：`/tmp` 那次运行在沙箱外执行（沙箱内 dyld 装载被拦，报的是沙箱错误而非本结论），但这不影响判据——`install_name` 是库的只读元数据，`otool -D /tmp/nxhost/build/bin/libnexcore.dylib` 直接给出相对路径。换 cwd 就能跑通，反过来证明失败原因不是「库缺失」而是「相对 install_name」。

### A6 泵在 ABI 面不可见

```
$ grep -rn "js_std_add_helpers\|js_std_loop" src/ | wc -l
0
$ grep -rln "pump" tests/
tests/pump_microtask.c
$ grep -c "pump" /tmp/nxhost/build/abi/NexCore.h
0
```
