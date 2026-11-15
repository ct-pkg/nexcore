# battle 材料：P1 是否允许依赖 QuickJS

- 议题：`docs/roadmap.md:53` 要求 P1 退出时「没有 QuickJS 依赖也能构建」，而 `src/NexCore.ct:74` 的 `nexcore_create` 无条件初始化 QuickJS。
- 本文只放两席材料，**不含裁定**。判据来源：主规范 7.x 的 battle 体例（四席 + 裁判），本轮只备两席。
- 标注约定：`[实测]` 本轮实际运行并贴原始输出；`[静态阅读]` 读码得出、未运行；`[未实测]` 需改被测代码或条件不具备，写明原因。
- 盘点时间：2026-11-15，HEAD `89de83d`。

---

## 0. 共享事实底册（两席共用，不偏袒）

| # | 事实 | 出处 |
| --- | --- | --- |
| F1 | P1 退出条件原文：「没有 QuickJS 依赖也能构建；`libnexcore` 的依赖只有 libc/pthread。」 | `docs/roadmap.md:53` |
| F2 | `nexcore_create` 在参数校验、开两个队列、`malloc` 之后，第一件事就是 `js = js_host::Host.init(cfg->bootstrap_js, (int32_t)cfg->bootstrap_len)`，失败即 `NX_E_INTERNAL` | `src/NexCore.ct:74-80` |
| F3 | 全仓仅 `src/js_host/js_host.ct` 一个文件 `#include "quickjs.h"`（4 行）、共 134 行；其余 `JS_` 命中都是统计键枚举名或注释 | `grep -rn "quickjs\|JS_[A-Z]" src/` |
| F4 | 公开 ABI 头 `NexCore.h.ct` 不含任何 QuickJS 类型/符号；生成物 `NexCore.h` 里 3 处 `JS_` 全是 `NX_STAT_JS_TURNS / JS_JOBS / JS_EXCEPTIONS` 枚举名 | `[实测]` 见 §3.1 |
| F5 | `js_host` 对 QuickJS 的符号依赖是 10 个（`JS_NewRuntime`、`JS_NewContext`、`JS_Eval`、`JS_JSONStringify`、`JS_ToCStringLen2`、`JS_GetException`、`JS_FreeValue`、`JS_FreeContext`、`JS_FreeRuntime`、`JS_FreeCString`），该单元未定义符号共 13 个（余下为 `malloc/free/memcpy`） | `[实测]` 见 §3.3 |
| F6 | JSON 编解码没有独立实现，靠 `JS_JSONStringify`（`js_host.ct:97`）与注释声明的 `JS_ParseJSON`（`src/protocol/protocol.h.ct:9`） | 静态阅读 |
| F7 | QuickJS 源码不入库：`.gitignore` 忽略 `experiments/third_party/`，无 vendoring、无 submodule、无 commit pin；`experiments/build.sh` 只固定**版本串** `QJS_VERSION=2025-09-13` 并从 `bellard.org` 现场下载 | `.gitignore:8`、`experiments/build.sh:17,31-39` |
| F8 | 缺 QuickJS 时全量检查在 ②b 处红：`src/js_host/js_host.ct:4:10: fatal error: 'quickjs.h' file not found`；而 ①a（公开头）与 ②a（生成实现 C）不受影响 | `[实测]` 见 §3.2 |
| F9 | 5 个测试二进制里 4 个直接链 `$QJS/libquickjs.a`（`stat_keys`、`bootstrap`、`drain_e2e`、`smoke`），`queue_fifo` 不链 | `tools/check.sh:283,296,303,310` 与 `:288-290` |
| F10 | `nexcore_create` 中的 js 相关调用仅 4 处：`Host.init`（:74）、`Host.eval`（:151，`pump_inbox`）、`Host.shutdown`（:226、:246） | `grep -n "js_host::Host" src/NexCore.ct` |

---

## 1. 主张席：认下 QuickJS 是 P1 的合法依赖

### 1.1 立场

QuickJS 不是"混进 P1 的越界依赖"，而是"桥接层的固有组成部分"：本项目的交付物第一条就是「QuickJS 运行时集成」（`docs/README.md:6`）。P1 的骨架若不带 JS，验收对象只剩环形队列和参数校验，交付价值与 `roadmap.md` P1 的产出表（loopback、多线程 send、两阶段关闭）不匹配——那些产出本来就必须经过一次 JS 求值才能闭环。

### 1.2 依赖面到底有多大（按实测）

- **代码面**：1 个单元 / 134 行 / 10 个外部符号（F3、F5）。占 `src/` 827 行的 16%。
- **ABI 面**：零渗透（F4）。`NexCore.h` 里没有 `JSRuntime*`、没有 `JSValue`，宿主拿到的只有不透明 handle + 纯 C 结构体。这是本项目最重要的既有资产：**换引擎不改 ABI**。
- **构建面**：①a 公开头、②a 生成实现 C 都不需要 QuickJS；只有 ②b 起的严格编译与 4 个测试二进制需要（F8、F9）。
- **替换成本**：换 Bellard → quickjs-ng 或换别的引擎，改动面被 `js_host.ct` 一个文件框住（F3），但**必须补一个 JSON 实现**（F6 的 `JS_JSONStringify` / `JS_ParseJSON` 是引擎能力，不是本项目实现）。这是主张席必须承认的隐藏成本。

### 1.3 不可复现怎么解（三档，按代价升序）

| 档 | 做法 | 判据 | 代价 |
| --- | --- | --- | --- |
| ① 版本串 + 摘要 | `experiments/build.sh` 固定 tarball 的 sha256（不只是 `QJS_VERSION`），下载后校验 | 摘要不符即失败，红在构建第一步 | 最小；但离线环境仍不可构建 |
| ② 固定上游 commit | 改成从固定 commit 拉源码（`QJS_COMMIT=<sha>`），`libquickjs.a` 由本地 `make` 产出 | 记录 commit；`git log` 可追 | 中；需处理上游仓库选择（与 Q-01 绑定） |
| ③ vendoring / submodule | 把 QuickJS 源码树入库（或 submodule 指向固定 commit），`.gitignore` 去掉 `third_party/` | 仓库自包含，`git clone` 后离线可构建 | 大；库体积与许可证文件入库，`check.sh` 的 `experiments/` 语义变重 |

主张席建议至少做到 ①，并把「离线可构建」列进 P1 退出判据（见 1.4 第 6 条），把 ③ 留给 P2 再议——因为 P1 只需要能构建，不需要分发。

### 1.4 按本席，P1 退出条件逐条写成

> 替换 `docs/roadmap.md:53` 那一条，其余四条不动：

1. ACC-A-01..A-03、A-05、A-06、A-08..A-10 通过；ACC-C-02、C-08、C-09（基础部分）通过。ACC-C-01/C-05/C-06 依赖 JS 线程，在 P2 关闭。
2. 阶段门禁：Q-06（平台集成形态与最低版本）必须在 P1 退出前关闭。
3. **QuickJS 为 P1 合法依赖**，依赖面限定为 `src/js_host/` 一个单元；`libnexcore` 的直接依赖是 libc/pthread + QuickJS 静态库，**不得**再引入第四个第三方依赖。
4. **公开 ABI 不暴露引擎类型**：生成物 `NexCore.h` 中不得出现 `JS_*`、`JSRuntime`、`JSValue`（现有实测为 0 处，作为冻结项守住）。
5. 所有 `.ct` 通过 `check-boundaries.sh`（该脚本当前不存在，见 P1 退出条件第 4 条的既有欠账）。
6. **QuickJS 源可复现**：`experiments/build.sh` 校验下载物 sha256（档 ①），失败即红；`tools/check.sh` 在 ②b 之前增加「QuickJS 存在 + 摘要匹配」的前置检查，缺失时报出应执行的构建命令，而不是让 `cc` 抛 `quickjs.h not found`。
7. `NexCoreConfig` 的 `_Static_assert` 在 macOS/Android 两个工具链都通过。

### 1.5 选本席要改的文件

`docs/roadmap.md:53`（改判据）、`experiments/build.sh`（加摘要校验/固定 commit）、`tools/check.sh`（加 QuickJS 前置检查）、`.gitignore`（仅当选档 ③）、`docs/README.md:19` 与 `docs/tutorial.md:6,98`（基线声明本来就要随 CT 升级改写）。**不动** `src/`。

### 1.6 核心代价一句话

**用"承认依赖 + 锁死可复现"换掉一次解耦改造，代价是 `check.sh` 新增一条前置依赖检查、`build.sh` 新增摘要校验，以及必须守住"ABI 不渗透引擎类型"这条冻结线。**

---

## 2. 反证席：P1 必须不依赖 QuickJS

### 2.1 立场

`roadmap.md:53` 写的不是笔误，而是 P1/P2 的分界判据：P1 交付「ABI + 传输骨架」，P2 才交付「QuickJS 嵌入 + 协议」。现在 `create` 里第一件事就是起 JS 运行时（F2），等于把 P2 的入口搬进了 P1，导致三个后果：①P1 的验收对象被 JS 引擎的能力绑架（引擎一换，P1 重测）；②QuickJS 源码不可复现（F7），一个"骨架阶段"却被一个不可复现的第三方库卡住构建；③宿主只要调用 `nexcore_create` 就吃下 QuickJS 的静态链接体积与许可证。

### 2.2 解耦要动多少现有代码

- `js_host.ct` 134 行变为「后端接口 + QuickJS 实现」两件：接口面只需 `init/eval/shutdown/turns` 四个函数（`js_host.ct:27,61,124,131` 已经就是这个形状），改成函数指针表或 CT 的 trait 分派。
- `NexCore.ct` 4 处调用点改为经接口间接调用（F10），`create` 增加"后端可选"分支：未提供后端时 `js = 0`，`send`/`drain` 走无 JS 路径（直接把 inbox 消息原样搬到 outbox，即 `roadmap.md:44` 的 loopback）。
- 4 个测试二进制需要拆出无 JS 变体（F9），或者让 `queue_fifo` 式的纯骨架测试成为 P1 的门禁主体。
- 新增：一个 `null` 后端（约 30-50 行）用于 P1 验收。

`[未实测]` CT 侧是否支持函数指针表/trait 动态分派——本仓 `src/` 内既有代码只用 `js_host::Host.xxx` 的静态命名空间调用与 `use Trait` 声明（`docs/codegen.md:201` 提到 `use Trait`），没有运行过"运行时选后端"的探针。这是本席最大的技术不确定性，落案前必须先做探针。

### 2.3 会不会把 create 的心智模型搞复杂

会，但幅度可量化：

- 现状：`create` = 校验参数 → 开两队列 → 起 JS → 填字段。失败只有 `NX_E_INTERNAL`。
- 解耦后：`create` 多一个"后端未就绪"分支，多一个错误码或降级语义；调用方必须知道"我这次 create 有没有 JS"——否则 `send` 的语义随后端变化而不可预测。
- 真正的坑不在 `create`，在 `send`/`drain`：无 JS 后端时消息是直通还是丢弃？直通就等于承认"JS 只是可插拔的处理器"，这会把 P2 的协议语义提到 P1 来决定。

### 2.4 按本席，P1 退出条件逐条写成

1. ACC-A-01..A-03、A-05、A-06、A-08..A-10 通过；ACC-C-02、C-08、C-09（基础部分）通过。ACC-C-01/C-05/C-06 依赖 JS 线程，在 P2 关闭。
2. 阶段门禁：Q-06（平台集成形态与最低版本）必须在 P1 退出前关闭。
3. **QuickJS 不在 P1 依赖内**：`libnexcore` 的依赖只有 libc/pthread；`nexcore_create` 在无 JS 后端时返回成功，且 loopback（`send → drain`）语义完整可用。
4. **引擎类型与引擎头文件不得出现在 `src/` 的 P1 单元**：`grep -rn "quickjs.h\|JS_[A-Z]" src/ --exclude-dir=js_host` 命中数为 0（现为 0，作为守线项）。
5. 所有 `.ct` 通过 `check-boundaries.sh`（脚本本身仍待补）。
6. **无 JS 构建可复现**：`git clone` 后不下载任何第三方源码即可 `tools/check.sh` 绿（P1 门禁不依赖 `experiments/third_party/`）。
7. `NexCoreConfig` 的 `_Static_assert` 在 macOS/Android 两个工具链都通过。

### 2.5 选本席要改的文件

`src/js_host/`（拆接口 + 实现）、`src/NexCore.ct`（4 处间接化 + `create` 分支）、`tools/check.sh`（无 JS 变体的门禁与测试路径）、`tests/`（至少一个不链 `libquickjs.a` 的骨架用例）、`docs/roadmap.md:53`（若要改写措辞）与 `docs/async-threading.md`（若直通语义变化需回写）。

### 2.6 核心代价一句话

**用"一次解耦改造 + 新增无 JS 后端 + 提前决定 send/drain 直通语义"换 P1 的干净边界与可复现，代价是 P1 的交付价值下降（没有 JS 的骨架只是回显队列），且后端动态分派在 CT 侧尚未验证。**

---

## 3. 实测证据（原始输出）

### 3.1 公开 ABI 头不依赖 QuickJS

```
$ cd /tmp/nxgate2 && ct build --emit-c --ns-root src src/NexCore.h.ct -o /tmp/abi-noqjs
cc -std=c99 -I /tmp/abi-noqjs /tmp/abi-noqjs/NexCore.c -o a.out
RC=0
$ ls -l /tmp/abi-noqjs/NexCore.h
-rw-r--r--  2572 NexCore.h
$ grep -n "JS_\|quickjs" /tmp/abi-noqjs/NexCore.h
46:    NX_STAT_JS_TURNS = 15,
47:    NX_STAT_JS_JOBS = 16,
48:    NX_STAT_JS_EXCEPTIONS = 17,
```

结论：生成公开头这一步不传 `-isystem quickjs` 也成功，且头里只有枚举名，没有引擎符号。

### 3.2 缺 QuickJS 时红在 ②b

```
$ cc -std=c99 -Wall -Wextra -Werror -I build/impl -I src/js_host -c build/impl/js_host.c -o /tmp/js_host_noqjs.o
src/js_host/js_host.ct:4:10: fatal error: 'quickjs.h' file not found
    4 | #include "quickjs.h"
      |          ^~~~~~~~~~~
1 error generated.
RC=1
```

### 3.3 依赖符号面

```
$ cc -std=c99 -Werror -I build/impl -I src/js_host -isystem "$QJS" -c build/impl/js_host.c -o /tmp/js_host_qjs.o
$ nm -u /tmp/js_host_qjs.o | grep "JS_" | sed 's/^ *//' | tr '\n' ' '
_JS_Eval _JS_FreeCString _JS_FreeContext _JS_FreeRuntime _JS_GetException _JS_JSONStringify _JS_NewContext _JS_NewRuntime _JS_ToCStringLen2 ___JS_FreeValue
$ nm -u /tmp/js_host_qjs.o | wc -l
      13
```

结论：10 个引擎符号 + 3 个 libc 符号，全部集中在 `js_host.o`；`NexCore.o` 的引擎依赖经 `js_host` 间接发生。

### 3.4 `[未实测]` 的两项

- **「去掉 `create` 里的 QuickJS 初始化后还能不能过」**：需要改 `src/NexCore.ct` 的被测代码；本轮纪律明确不改 `src/`，在副本上改会变成"测的不是仓库代码"，故不做。静态阅读的结论是：`create` 中除 `:74` 外没有 js 相关调用（其余为队列、`malloc`、字段赋值），所以"去掉即成功"在代码结构上成立，但**没有运行证据**。
- **「CT 是否支持运行时选后端（函数指针表 / trait 动态分派）」**：需要写探针，本轮未做。这一项直接决定反证席 2.2 的可行性。

---

## 4. 裁定所需的最小区分问题（不是裁定）

1. P1 的验收对象到底是「ABI + 传输骨架」还是「能跑通一次 JS 求值的桥接骨架」？两种答案直接决定 F1 该保留还是改写。
2. 「不可复现」是必须在本阶段消灭的问题，还是可以带进 P2？主张席的档 ① 只需改 `build.sh` 十余行；反证席的方案要动 `src/` 三个文件加测试。
3. 宿主能不能接受 `libnexcore` 静态链入 QuickJS？若可以，反证席的第 3 条价值大幅下降；若不可以（体积/许可证/合规），主张席必须给出"P1 交付物不含 JS"的替代定义。

---

## 5. Q-06 现状（与 QuickJS 依赖无关）

- **原文**：`docs/open-questions.md:20` —「| Q-06 | Swift/Android 集成形态与最低系统版本 | P1 | OPEN | **P1 出口前** | 平台负责人 |」；正文在 `docs/open-questions.md:124-137`，内容是 SPM/XCFramework、AAR 还是源码模块、JNI 是否独立 `.so`、是否要求 Swift 6 严格并发，临时默认「SPM system library；iOS 15+/macOS 12+；Android minSdk 26、JNI 层独立 `.so`、Kotlin 协程封装」。
- **状态**：`OPEN`，`roadmap.md:54` 把它列为 P1 退出的硬前置。
- **与 QuickJS 的关系**：无。它约束的是平台分发形态与 JNI 分层，不涉及引擎选型。**本议题不为它出两案。**
- **真正相关的是 Q-01**（`docs/open-questions.md:21`）：「QuickJS 选 Bellard 还是 quickjs-ng，锁定哪个版本 | P1 | OPEN | P2 编码前 | 技术负责人」。主张席 1.3 的档 ② 与它绑定；裁定前应先看 Q-01 的倾向。
