# nexcore 现状（2026-11-15）

> 本文是一份**状态文档**，不是对话纪要。所有事实取自仓库 `main` @ `808fc53` 与本机实测；
> 每条尽量给 `文件:行号` 或提交号。无法核对的推测标 `[未验证]`。

---

## 1. 一句话结论

nexcore 处在**「骨架能构建、契约未裁定」**的阶段：`tools/check.sh` 全量与地基验证都能跑绿（`ALL CHECKS PASSED`，rc=0），但按规范 1.9.0 它还**不能直接开工**——卡在 P1 最关键的一条自相矛盾（「不依赖 QuickJS」vs `create` 里初始化 QuickJS），以及 `docs/phases/`、`issues/`、`docs/snippets/`、`REQ-*` 四类台账全缺。

---

## 2. 仓库快照

| 维度 | 事实 | 出处 |
| --- | --- | --- |
| 跟踪文件 | 121 个；另有未跟踪目录 `2026-11-12/`（旧分析产物） | `git ls-files \| wc -l`；`git status --short` |
| 语言与行数 | `.ct` 68 文件 / 1849 行；C 8 / 484；Python 2 / 378；Shell 3 / 640；Markdown 31 / 10109；Swift 2 / 48；JSON 1 / 16；JS 1 / 32 | `git ls-files '*.<ext>' \| xargs wc -l` |
| 实现核心 | `src/` 11 文件 / 843 行（含 `units.json` 16 行），主体 `src/NexCore.ct` 318 行、`src/NexCore.h.ct` 125 行 | `git ls-files src \| xargs wc -l` |
| 构建入口 | `tools/check.sh`（418 行，全量门禁唯一入口）；`experiments/build.sh`（CT↔QuickJS 地基验证）；`tools/nexcore_gen.py`（297 行生成器） | 三文件本身 |
| 目录要点 | `src/{NexCore.ct, NexCore.h.ct, units.json, protocol/, queue/, js_host/}`、`tests/`（5 个 C 夹具 398 行）、`abi/`、`tools/`（3 文件）、`experiments/`（70 文件，多为能力探针）、`docs/`（含 `spec/` 规范副本与 `reviews/`）、`reviews/`（7 份设计评审）、`.trae/` | `git ls-files \| cut -d/ -f1 \| sort \| uniq -c` |
| 提交历史 | 6 个提交，跨度 2026-11-12 → 2026-11-15 | `git rev-list --count HEAD`；`git log --reverse --date=short` |
| 声明状态 | `v0.9`，并声称 `tools/check.sh` 已全量实跑通过（`SCRIPT_RC=0`） | `docs/README.md:17` |

要点：**这条仓库不是纯文档仓**，`src/` 已有可构建的 CT 实现（827 行 `.ct`），`experiments/` 是更大的探针集合但不是产品代码。

---

## 3. CT 基线

| 项 | 值 | 出处 |
| --- | --- | --- |
| 原声明 | `c27da3d`（v0.19.1） | 升级前 `docs/README.md:19`、`docs/tutorial.md:98` |
| 现声明 | `ct v0.44.1`，`ct-lang` tag `v0.44.1` = `158d7f40`，升级日期 2026-11-15 | `docs/README.md:19`、`docs/tutorial.md:6,98`、`experiments/README.md:4`、`experiments/build.sh:5`（提交 `808fc53`） |
| 本机二进制 | `ct v0.44.1`（`/Users/samxx/.local/bin/ct`，不在默认 PATH） | `PATH="$HOME/.local/bin:$PATH" ct --version` |
| 源码 HEAD | `b8d50c3f`，最新 tag `v0.44.2`（`328e5e63`） | `git -C ct-lang describe --tags --abbrev=0` |

**偏差**：二进制 v0.44.1 比 `ct-lang` 的 `v0.44.2` 落后一个补丁版本。本文所有实测都基于 v0.44.1；v0.44.2 的行为未验证，标 `[未验证]`。

### 3.1 升级时修掉的 2 处不兼容

基线从 v0.19.1 跳到 v0.44.1（跨 25 个次版本），实测只暴露 2 处，都在 `experiments/build.sh` 的「已知语言缺口」探针里，产品代码 `src/` 零改动：

| # | 位置 | 旧期望 | 新行为（原文） | 归类 |
| --- | --- | --- | --- | --- |
| 1 | `experiments/build.sh:135` | `'import' is not supported in compile mode` | `import_in_compile_mode.ct:7:1: error: 'import' requires multi-file mode: use 'ct build'` | CT 诊断文案变更 |
| 2 | `experiments/build.sh:150-155` | import 写在 `namespace` 之后「不报错、但 `#include` 被静默丢弃」 | `import_after_ns.h.ct:3:1: error: imports must be at the top of the file; move this import above the first declaration` | CT 语义变更（缺口被修掉，旧探针断言失效） |

第 1 处只改期望串；第 2 处把「断言被丢弃」改成 `expect_fail` 断言报错。另有两条探针（`errno_probe`、`ifdef_probe`）的旧期望串在新版仍匹配，未动。本轮**未发现 CT 自身缺陷**，因此没有需要登记给 CT 仓库的提案材料。

### 3.2 修复后实测

仓库 `build/` 目录被 macOS `com.apple.provenance` 封存（`rm`/`mkdir`/`xattr` 全 EPERM），所有跑测都在副本 `/tmp/nxb2` 完成，QuickJS 静态库以符号链接复用已构建的 `libquickjs.a`：

| 命令 | 结果 | 耗时 |
| --- | --- | --- |
| `bash tools/check.sh` | rc=0，末行 `ALL CHECKS PASSED` | real 6.27s |
| `bash experiments/build.sh` | rc=0，末行 `== ALL CHECKS PASSED ==` | real 7.65s |
| `bash tools/check.sh g0` | rc=0，`ok g0 (block 1.9.0/b8651332, landing 3 段, 8 行, session-map, 版本落点)` | — |

升级前状态（v0.19.1）无法复现——本机没有该版本二进制，标 `[未实测]`。首跑 v0.44.1 时的失败点就是上表第 1 处（`FAIL: import_in_compile_mode failed with an unexpected message`）。

### 3.3 仍残留的旧引用

以下文件仍写着 `c27da3d` / `v0.19.1`，但**不属于本次改动范围**，需要单独裁定是否刷新：

| 位置 | 原文性质 | 建议 |
| --- | --- | --- |
| `docs/design.md:250` | 「结论来自本机 `ct`（`c27da3d`）实测」 | 历史实测记录，可在下一轮复验后更新为「v0.19.1 实测 + v0.44.1 复验」 |
| `docs/acceptance.md:46` | 验收环境行 `CT c27da3d` | 属活文档，应随基线改为 `158d7f40` |
| `docs/open-questions.md:291` | 「`c27da3d` 的实测结论可能在新版本变化」 | 事实描述，保留 |
| `docs/open-questions.md:295` | 「固定 `c27da3d`（v0.19.1）作为 v1 基线；CI 使用固定 commit」 | **与现声明直接冲突**，必须重裁（见 §6.4） |
| `docs/session-map.md:48` | 原「基线冲突只登记、未改声明」 | 本轮已改为「2026-11-15 已重定」 |

`reviews/`（设计评审）与 `2026-11-12/`（旧分析交付）属带日期的历史记录，按原样保留。

---

## 4. 门禁 G0–G9 现状

| 门禁 | 判定 | 缺什么 | 现在跑得到什么 |
| --- | --- | --- | --- |
| G0 规约纳管 | 部分具备 | 主规范 §11.5 L-7 纳管清单未收录 nexcore（需在 `.ai-engineering` 侧加一行，仓库外） | `g0` 已实现并绿：校验生成块头/尾存在、`version` 与 `sha256` 等于 `docs/spec/ai-coding-workflow.md` 实算值、块体与 `sync-agents.sh --print` 逐字相同（工具缺失时只 WARN 不冒充通过）、块外三段齐全（`### 0.1 本项目实例化` / `### 0.2 话术路由` / `### 0.3 偏离项`）、实例化表 8 行、`版本落点` 非空且 `docs/README.md` 确有 `- 状态：v…` 行、`docs/session-map.md` 存在 |
| G1 主张完整 | 部分具备 | 无 `REQ-*` 需求 ID；无 `docs/phases/PH-<n>/card.md`（无任务清单七列、无 `version-minor`）；无 `g1` | 只有 `docs/roadmap.md` 的 P1..P4 段落 + `docs/acceptance.md` 的 64 条 `ACC-*` |
| G2 设计可验收 | 部分具备 | 无「接口承诺 → `ACC-*`」机械映射表；无 `g2` | 可人工对照 `docs/design.md`、`docs/protocol.md` 与 `ACC-*` |
| G3 构建与单测 | 具备（非门禁化） | 无 `g3` 子命令；需预先备好 QuickJS 静态库 | 全量 `tools/check.sh` 15 项检查全绿，`-Werror` 零警告，`tests/` 5 个 C 用例 + smoke 通过 |
| G4 文档可执行与时效 | 缺失 | 无 `docs/snippets/`；文档无 `applies-to`；5 份文档超 300 行（`design.md` 723、`codegen.md` 703、`tutorial.md` 468、`async-threading.md` 427、`ai-friendly.md` 320） | 无 |
| G5 测试自证 | 缺失 | 无红证留存（无带 `baseline: <sha>` 的失败记录）；`reviews/` 是设计评审，不算红证 | 无 |
| G6 台账闭合 | 缺失 | 无 `issues/`，无 `status` 字段可查 | 无 |
| G7 安全机械项 | 缺失 | 无 ASAN/TSAN 目标、无密钥扫描（`grep -n "sanitize\|asan"` 在 `tools/check.sh`、`experiments/build.sh` 无命中） | 无 |
| G8 提交粒度与范围 | 缺失 | 无 `g8`；无 phase/tag/版本号体系可校验 | 无 |
| G9 规范锚定 | 具备 | —（当前 pin 与主规范一致） | `AGENTS.md` 块头 `version=1.9.0` / `sha256=b8651332…b4520dc`，与 `docs/spec/` 副本实算值一致 |

`tools/check.sh` 的分派只有三路：无参数 → 全量；`g0` → 规约门禁；**其余任何参数 → 打印 `usage: bash tools/check.sh [g0]（g1..g9 未实现）` 并退出码 2**（`tools/check.sh:101-110` 的分派块）。这是刻意设计——「不能跑的门禁不许装绿」。

---

## 5. 文档与现实的矛盾

不止「文档过时」，有三处是**结构性矛盾**，会直接影响 P1 是否成立：

1. **「不包含实现代码」vs 已有实现**
   `docs/README.md:15` 写「本目录只描述契约与边界，不包含实现代码」，但仓库已有 `src/` 11 文件 / 843 行（含 827 行 `.ct`）与 `tests/` 5 个 C 夹具 398 行，且 `tools/check.sh` 会对它们做 `-Werror` 与 `nm` 符号校验。

2. **P1 退出条件「不依赖 QuickJS」vs `create` 里的引擎初始化**
   `docs/roadmap.md:55` 要求「没有 QuickJS 依赖也能构建；`libnexcore` 的依赖只有 libc/pthread」；而 `src/NexCore.ct:74` 在 `nexcore_create`（`src/NexCore.ct:42`）里第一件要做的事就是 `js = js_host::Host.init(cfg->bootstrap_js, …)`，失败即返回 `NX_E_INTERNAL`。这不是「文档过时」级别的偏差，是**退出判据与实现互相否决**。

3. **P1 退出条件引用的 `check-boundaries.sh` 不存在**
   `docs/roadmap.md:56` 要求「所有 `.ct` 通过 `check-boundaries.sh`」，`docs/ai-friendly.md:142` 还给了该脚本的小节；但仓库无 `scripts/` 目录，`tools/` 只有 `check.sh`、`nexcore_gen.py`、`scan_c.py`。`docs/acceptance.md:130`（ACC-F-01）也依赖它。

补充一处轻量不一致：`docs/README.md:17` 的状态仍写检查项编号为 `0 / ①a~①c / ②a~②c / ③ / ④ / ⑤a ⑤b / ⑥ / smoke`，而 `tools/check.sh` 已新增 `g0` 子命令（`tools/check.sh:18` 的用法段）。

---

## 6. 未决问题

### 6.1 QuickJS 依赖之裁（`docs/reviews/nexcore-p1-quickjs-battle.md`，提交 `30f5a7e`）

两席各自的核心代价：

- **主张席（认下依赖）**：把「不可复现」升级成「可校验的不可复现」——用 `experiments/build.sh` 的版本+摘要校验、`tools/check.sh` 的前置检查锁死，代价是 `js_host` 依赖的 10 个引擎符号永久挂到 P1 的构建前提上（公开 ABI 头本身不受污染）。
- **反证席（必须不依赖）**：拆后端接口 + 无 JS 后端可换到干净的 P1 边界，代价是 P1 交付价值下降，且 CT 侧动态分派在 v0.44.1 上未验证。

实测三件事（命令与原始输出在 battle 文档 §3）：

1. 公开 ABI 头生成不依赖 QuickJS——`ct build --emit-c src/NexCore.h.ct` rc=0，生成头内 **0 个**引擎符号。
2. 缺 `quickjs.h` 时红在 ②b，原文 `src/js_host/js_host.ct:4:10: fatal error: 'quickjs.h' file not found`。
3. `js_host` 引用 **10 个**引擎符号（该单元共 13 个未定义符号），即依赖集中在 `js_host` 一处，不是散布全仓。

裁定会牵动：`docs/roadmap.md:55`（退出条件）、`src/NexCore.ct:74`（`create` 里的初始化）、`src/js_host/`（三个文件）、`experiments/build.sh`（QuickJS 拉取与校验）、`tools/check.sh`（②b/②c 前提）、`docs/design.md`（桥接层边界）、`docs/open-questions.md`（Q-01 收口）、`.gitignore` 与 vendoring 方案。

### 6.2 Q-06 的实际内容，以及它为何与 QuickJS 无关

`docs/open-questions.md:20` 与 §`Q-06`（`docs/open-questions.md:124-132`）：**Swift/Android 集成形态与最低系统版本**——Swift 用 SPM system library + modulemap 还是 XCFramework、最低 iOS/macOS 版本、Android 用 AAR 还是源码模块、JNI 是 `libnexcore.so` 直接导出还是再加 `libnexcore_jni.so`、是否要求 Swift 6 严格并发。状态 `OPEN`，最晚决策点「P1 出口前」（`docs/roadmap.md:54` 同）。

它**不涉及**引擎选型：Q-06 的每一条都是分发形态与系统 API 边界，没有一条引用 QuickJS 符号或头文件。与 QuickJS 真正相关的是 **Q-01**。

### 6.3 Q-01 的地位

`docs/open-questions.md:15`：QuickJS 选 Bellard 还是 quickjs-ng、锁定哪个版本；状态 `OPEN`，最晚决策点「P2 编码前」，负责人技术负责人。§`Q-01`（`docs/open-questions.md:38` 起）指出两个 fork 在 `JS_NewClassID` 签名、构建系统、`JS_NewCFunction` 宏与 Atom 接口上不同，绑定层写死后再换 fork 要改 shim 与所有调用点。

也就是说：**Q-01 决定「依赖哪个引擎」，6.1 决定「要不要依赖引擎」**。前者是 P2 的门，后者是 P1 的门，顺序不能反。

### 6.4 基线本身是否需要再裁一次

需要。理由有两条，都是硬冲突：

- `docs/open-questions.md:295` 仍以「固定 `c27da3d`（v0.19.1）作为 v1 基线；CI 使用固定 commit」为默认，与 `docs/README.md:19` 的新声明（v0.44.1 / `158d7f40`）直接矛盾。
- 跨 25 个次版本只暴露 2 处探针不兼容，说明此前 v0.19.1 时期的 FIX/GAP 结论**大多需要在新版复验**（`docs/reviews/ct-capability-audit.md`、`docs/design.md:250` 的结论都基于 `c27da3d`）。复验完成前，「CT 能力边界」一节只能当历史结论用。

---

## 7. 阻塞项与最短路径

按就绪度盘点（`docs/reviews/nexcore-readiness.md` §6）的口径，未闭合 8 项，按必补顺序：

| 顺序 | 项 | 现状 | 不补会怎样 |
| --- | --- | --- | --- |
| 1 | G0 收口 | `session-map.md` 与 `g0` 已落（`89de83d`）；仍缺主规范 L-7 收录 | 按 L-3 永远判「未纳管」，开了 phase 也不合规 |
| 2 | G1 建档 | 缺 `PH-01` 卡、缺 `REQ-*`、两处 P1 矛盾未裁 | 门禁给不出「主张完整」，任何实现提交都缺上游判据 |
| 3 | G2 映射 | 缺接口→`ACC-*` 机械映射 | 「设计可验收」只能靠人工口头保证 |
| 4 | G3/G5/G7 机械化 | G3 实质具备但无 `g3`；G5、G7 全空 | 现有全绿只证明「能构建」，不构成规范意义上的自证；无 ASAN/密钥扫描则安全项无证据 |
| 5 | G4 文档可执行 | 无 `snippets/`，5 份文档超 300 行 | 文档命令不可复跑，评审只能读不能验 |
| 6 | G6 台账 | 无 `issues/` | 问题无处闭合，phase 收口无依据 |
| 7 | G8 提交粒度 | 无 `g8`、无 phase/tag 体系 | 提交无法按 phase 校验，回滚无边界 |
| 8 | G9 之外的 pin 收尾 | 块头 pin 一致，但 `open-questions.md:295` 旧 pin 未清 | 两套基线并存，任何「按基线实测」的结论都会被质疑 |

**三步走**：

1. **先裁 QuickJS 依赖**（6.1），把结论写回 `docs/roadmap.md:55` 与 `src/NexCore.ct:74` 的边界描述，同时立 `PH-01` 卡把 P1 退出条件逐条写成可核对判据——这一步不做，第 2 步的门禁没有判据可检。
2. **把门禁变成能跑的东西**：补 `g3`（复用现有 15 项检查）、`g7`（ASAN/TSAN + 密钥扫描）、`g5`（红证留存规范），让「红/绿」由脚本说话。
3. **再做文档与台账体系**：`docs/snippets/` + 文档拆到 ≤300 行 + `applies-to`（G4）、`issues/`（G6）、提交粒度校验（G8）。

---

## 8. 本次已落地的提交

| 提交 | 一句话 |
| --- | --- |
| `ef3b02d` | 2026-11-12 初始提交：NexCore 文档契约、CT 骨架、实验验证与教程 |
| `7a5c2a1` | 纳管工程规范 1.9.0：`AGENTS.md` 生成块 + `docs/spec/` 全文副本（sha256 `b8651332…`） |
| `b19b2ba` | 就绪度盘点：按 G0–G9 逐项判定，结论「需先补 8 项」 |
| `89de83d` | 落 `docs/session-map.md` 并新增 `g0` 规约纳管门禁（正反两路实测过） |
| `30f5a7e` | P1 是否依赖 QuickJS 的两席材料（不自行裁定，含三项实测） |
| `808fc53` | CT 基线升级到 v0.44.1（`158d7f40`）+ 修两处探针不兼容 |

> 更正：`46e7196` 不在本仓库历史里，它是 `ct-fsguard` 的规范纳管提交（`docs(spec): 纳管工程规范 1.9.0`），一并列在此处易误读。

---

## 附：本文的实测环境

- `ct v0.44.1`（`/Users/samxx/.local/bin/ct`，需显式加 PATH）；`ct-lang` HEAD `b8d50c3f`，tag `v0.44.2`
- QuickJS `2025-09-13`，静态库由 `experiments/build.sh` 构建；`experiments/third_party/` 被 `.gitignore` 忽略，不在仓库内
- 跑测位置：仓库副本（原仓库 `build/` 被 `com.apple.provenance` 封存，`rm`/`mkdir`/`xattr` 均 EPERM）；清理需在你自己的终端 `rm -rf /Users/samxx/trae-ws/nexcore/build`
