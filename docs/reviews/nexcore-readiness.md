# nexcore 按工程规范 1.9.0 推进的就绪度盘点

- 盘点对象：`/Users/samxx/trae-ws/nexcore`，HEAD 提交 `7a5c2a1`（`docs(spec): 纳管工程规范 1.9.0`）
- 盘点时间：2026-11-15（Asia/Shanghai）
- 判据来源：主规范 `1.9.0`（`/Users/samxx/trae-ws/.ai-engineering/methodology/ai-coding-workflow.md`）§6.4 G0–G9、§11.5 L-2/L-3/L-7
- 方法：只读阅读 + 只运行不写仓库的命令；门禁在仓库副本上实跑（见文末「实测环境」）
- 结论：**需先补 5 项硬缺失（G4–G8），并把 G0/G1/G2 的机械部分补齐；不建议现在直接开 phase**

---

## 1. 仓库实际形态

| 维度 | 事实 | 出处 |
| --- | --- | --- |
| 跟踪文件 | 118 个 | `git ls-files \| wc -l` |
| 代码量 | `.ct` 68 文件 / 1849 行，其中 `src/` 10 文件 / 827 行；C 侧 8 文件 / 507 行；Python 2 文件 / 378 行 | `git ls-files '*.ct' \| xargs wc -l` 等 |
| 文档量 | 28 个 `.md` / 9770 行（含 `docs/spec/ai-coding-workflow.md` 1319 行规范副本） | `git ls-files '*.md' \| xargs wc -l` |
| 主要语言 | CT（`.ct`，编译到 C）+ 少量 C 测试夹具 + Python 生成器 | 同上 |
| 构建入口 | `tools/check.sh`（333 行，全量检查唯一入口）；`experiments/build.sh`（地基验证）；`tools/nexcore_gen.py`（生成器） | [tools/check.sh](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L1-L33) |
| 目录结构 | `src/{NexCore.ct, NexCore.h.ct, units.json, protocol/, queue/, js_host/}`、`tests/`、`abi/`、`tools/`、`experiments/`、`docs/`、`reviews/` | `ls src tests abi tools` |
| 提交历史 | 2 个提交：`ef3b02d`（2026-11-12 初始提交）、`7a5c2a1`（2026-11-15 纳管规范） | `git log --oneline` |
| 未跟踪 | `2026-11-12/`（此前一次只读现状分析交付目录） | `git status --short` |

实现进度事实（本条基于代码与测试输出，不采信文档自述）：

- 已落地：C ABI 发布头与实现两趟构建、`queue` 环形队列、`js_host` 单线程 `eval → stringify`、`protocol` 最小分发、两阶段关闭、统计键。
- 未落地：`src/` 内无 `pthread` 任何使用（`grep -rln pthread src` 为空）→ 无 JS 线程；Promise / 超时 / 事件只有 `NexCoreConfig.default_timeout_ms` 一个字段（[NexCore.h.ct:92](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L92)），无实现。

---

## 2. 文档面

| 应有 | 现状 | 判定 |
| --- | --- | --- |
| `docs/requirements/`（需求条目 + ID） | 不存在；需求散在 `docs/design.md`、`docs/protocol.md`，无 `REQ-*` 编号 | 缺失 |
| `docs/roadmap.md` | 存在，但形态是 `P1..P4` 四阶段段落（[docs/roadmap.md:1-23](file:///Users/samxx/trae-ws/nexcore/docs/roadmap.md#L1-L23)），不是 `PH-<n>` 卡 | 部分具备 |
| `docs/phases/PH-<n>/card.md` | 不存在；仓库内无任何 `PH-` 编号或 `version-minor` 字段 | 缺失 |
| `docs/session-map.md` | 不存在（`grep -rn "session-map" docs/` 无命中） | 缺失 |
| `issues/` | 不存在 | 缺失 |
| `docs/decisions/` | 目录不存在；决策是单文件 `docs/decisions.md`（`D-01..D-19`） | 部分具备 |
| `docs/snippets/` | 不存在；文档里代码块为手写 | 缺失 |
| 验收条目 | `docs/acceptance.md` 有 64 条 `ACC-*`，每条带测量方法与证据格式 | 具备 |
| 未决问题台账 | `docs/open-questions.md` 20 条 `Q-*`，含优先级/状态/最晚决策点 | 具备 |

---

## 3. 门禁面：`tools/check.sh` 对 G0–G9 的覆盖

`tools/check.sh` 现有编号是 `0 / ①a ①b ①c / ②a ②b ②c / ③ / ④ / ⑤a ⑤b / ⑥ / ⑦ / ⑧ / ⑨ / smoke`（[tools/check.sh:16](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L16)），**没有 `g0..g9` 子命令，也没有任何参数解析**（`grep -n '\$1\|case\|usage' tools/check.sh` 无命中）。因此主规范要求「每个项目在 `tools/check.sh` 里提供同名子命令」这一层全部欠账。

| 门禁 | 判定 | 事实 |
| --- | --- | --- |
| G0 规约纳管 | 部分具备 | 生成块、`docs/spec` 副本、pin（`1.9.0` / `b8651332…`）已由 `sync-agents.sh --check` 验证通过；缺 `docs/session-map.md`、缺 `g0` 子命令 |
| G1 主张完整 | 部分具备 | `ACC-*` 有 64 条且带方法；但无 `REQ-*` 需求 ID、无 phase 卡（无任务清单七列、无 `version-minor`）、无 `g1` |
| G2 设计可验收 | 部分具备 | `design.md`/`protocol.md` 的承诺大体能对到 `ACC-*`；无机械映射表、无 `g2` |
| G3 构建与单测 | 具备 | 全量门禁实跑 `ALL CHECKS PASSED`（RC=0），`-Werror` 零警告，`tests/` 5 个 C 用例 + smoke 全绿；但无 `g3` 子命令，且需手工备好 QuickJS 静态库 |
| G4 文档可执行与时效 | 缺失 | 无 `docs/snippets/`、文档无 `applies-to`；超 300 行的文档 5 份（`design.md` 723、`codegen.md` 703、`tutorial.md` 468、`async-threading.md` 427、`ai-friendly.md` 未计副本）；无 `g4` |
| G5 测试自证 | 缺失 | 无红证留存，仓库内无 `baseline: <sha>` 形式的失败记录；`reviews/` 下是设计评审，不是红证 |
| G6 台账闭合 | 缺失 | 无 `issues/`，无 `status` 字段可查 |
| G7 安全机械项 | 缺失 | `grep -n "sanitize\|asan" tools/check.sh experiments/build.sh` 无命中：无 ASAN/TSAN 目标、无密钥扫描 |
| G8 提交粒度与范围 | 缺失 | 无 `g8`；历史仅 2 个提交，无 phase/tag/版本号体系可校验 |
| G9 规范锚定 | 具备 | `AGENTS.md` 块头 `version=1.9.0` / `sha256=b8651332d8b5e4db93f7a8718a3fc00c8a2c43627e1ce7145dbae82ecb4520dc`，与主规范实算一致 |

合计：**具备 2（G3、G9）/ 部分具备 3（G0、G1、G2）/ 缺失 5（G4、G5、G6、G7、G8）**。

**G0 的额外一票否决**：主规范 §11.5 L-7 的纳管清单只有 ct-lang、sam-utils 两行，nexcore 不在表内；同条明确「清单之外的项目即使有 `AGENTS.md`，也不算纳管」。按 L-3，未纳管即 G0 失败、不得开新 phase。

---

## 4. 现状声明与实际的距离

| 声明 | 出处 | 实际 |
| --- | --- | --- |
| 「本目录只描述**契约与边界**，不包含实现代码」 | [docs/README.md:15](file:///Users/samxx/trae-ws/nexcore/docs/README.md#L15) | 仓库已有 `src/` 827 行 `.ct` 实现、`tests/` 5 个 C 夹具、`tools/nexcore_gen.py` |
| 状态 v0.9：「`tools/check.sh` … 已全量实跑通过（`SCRIPT_RC=0`）」 | [docs/README.md:17](file:///Users/samxx/trae-ws/nexcore/docs/README.md#L17) | 本轮可复现：补齐 `ct` + QuickJS 后全量 `ALL CHECKS PASSED` |
| CT 基线 `c27da3d`（v0.19.1） | [docs/README.md:19](file:///Users/samxx/trae-ws/nexcore/docs/README.md#L19)、[docs/tutorial.md:98](file:///Users/samxx/trae-ws/nexcore/docs/tutorial.md#L98) | 本机 `ct v0.44.1`，与声明基线相差 25 个次版本；文档自己也要求换版本必须重跑校验 |
| P1 退出条件：「没有 QuickJS 依赖也能构建；`libnexcore` 的依赖只有 libc/pthread」 | [docs/roadmap.md:53](file:///Users/samxx/trae-ws/nexcore/docs/roadmap.md#L53) | `src/NexCore.ct:74` 在 `create` 里直接 `js_host::Host.init(...)`，QuickJS 是硬耦合；P1/P2 边界已被实现合并，退出条件本身需要重述 |
| P1 退出条件：「所有 `.ct` 通过 `check-boundaries.sh`」 | [docs/roadmap.md:56](file:///Users/samxx/trae-ws/nexcore/docs/roadmap.md#L56)、[docs/ai-friendly.md:142](file:///Users/samxx/trae-ws/nexcore/docs/ai-friendly.md#L142) | 仓库无 `scripts/` 目录，无 `check-boundaries.sh`；`tools/` 只有 `check.sh`、`nexcore_gen.py`、`scan_c.py` |
| P1 退出前必须关闭 Q-06 | [docs/roadmap.md:54](file:///Users/samxx/trae-ws/nexcore/docs/roadmap.md#L54)、[docs/open-questions.md:20](file:///Users/samxx/trae-ws/nexcore/docs/open-questions.md#L20) | Q-06 状态仍为 `OPEN`（Swift/Android 集成形态与最低系统版本） |

---

## 5. 未完成迹象

- `TODO / FIXME / TBD / 待定 / 尚未实现` 等标记共 9 处（`git ls-files | xargs grep -n ...`）；其中 `reviews/design-skeleton-review.md` 占 1 处 `TODO/FIXME`。
- 无显式 `stub` / `abort()` 占位实现（对应 grep 无命中），即"未实现"是以能力缺口而不是空函数形式存在：无线程、无 Promise、无超时、无事件订阅、无平台接入、无 Schema 生成。
- `experiments/` 下 68 个 `.ct` 中有相当部分是能力探针（`ns_probe`、`trait_abi`、`gaps`），不是产品代码；`src/` 只占 827 行。

---

## 6. 结论与第一步

**结论：不建议现在推进——需先补 5 项硬缺失（G4–G8）并补齐 G0/G1/G2 的机械部分，共 8 项未闭合。**

必补项与顺序（不补的后果写在括号里）：

1. **G0 收口**：新建 `docs/session-map.md`（四列：任务 / 对话 / 角色 / phase）、在 `tools/check.sh` 增加 `g0` 子命令；同时推动主规范 §11.5 L-7 收录 nexcore。（不补：按 L-3 永远判"未纳管"，G0 红，开了 phase 也不合规。）
2. **G1 建档**：把 `roadmap.md` 的 `P1..P4` 改写成 `PH-01` 卡（任务清单七列 + `version-minor` + 可核对的退出判据），需求补 `REQ-*` ID；同一轮先裁定两个已在文档里互相打倒的点——P1 退出条件「不得依赖 QuickJS」与 `NexCore.ct:74` 的硬耦合、以及 `Q-06` 仍 `OPEN`。（不补：G1 无法给"主张完整"结论，任何实现提交都缺上游判据。）
3. **G3/G5/G7 机械化**：`tools/check.sh` 拆出 `g0..g9` 子命令，先把现有 15 项检查归入 `g3`，补 ASAN/TSAN 与密钥扫描（`g7`），并建立红证留存（`g5`，含 `baseline: <sha>` 与原始输出）。（不补：现有全绿只证明"能构建"，不构成规范意义上的自证。）
4. 之后才是 G4（snippet 抽取 + 文档拆分 ≤300 行 + `applies-to`）与 G6（`issues/` 台账）、G8（按 phase 的提交粒度与 tag 校验）。

**第一步该做什么**：先建 `docs/session-map.md` 并在主规范 L-7 清单加 nexcore 一行，再写 `PH-01` 卡把 P1 的退出条件重述为可核对判据（含 QuickJS 依赖去留的裁决）——这三件事不落地，后面每一道门禁都会卡在同一处。

---

## 附：本轮实测环境与原始输出

- `ct`：`/Users/samxx/.local/bin/ct` → `ct v0.44.1`（默认 PATH 上无 `ct`，需 `PATH="$HOME/.local/bin:$PATH"`）
- QuickJS：`experiments/third_party/quickjs` 不在仓库内（`.gitignore` 忽略，需 `experiments/build.sh` 拉取），本轮按默认 `QJS_VERSION=2025-09-13` 现场构建出 `libquickjs.a`
- 无需 `CT_STD_DIR`（`tools/check.sh` 未读取该变量）
- 运行位置：仓库副本 `/tmp/nxgate`（原因见下），命令 `PATH="$HOME/.local/bin:$PATH" bash tools/check.sh`
- 末行原文：`ALL CHECKS PASSED`，退出码 `0`
- 未在原仓库直接跑的原因：`tools/check.sh` 第一步 `rm -rf build/abi build/impl build/bin` 被 macOS 拒绝，原始报错为 `rm: build/abi: Permission denied`、`mkdir: build/abi: Permission denied`。该 `build/` 目录是第一轮尝试时被沙箱创建后即被 `com.apple.provenance` 封存，后续任何进程（含 `xattr -dr`）都无法改写或删除：`xattr: [Errno 13] Permission denied: 'build'`。该目录在 `.gitignore` 内，不影响 `git status`；清理需在你自己的终端执行 `rm -rf /Users/samxx/trae-ws/nexcore/build`。
