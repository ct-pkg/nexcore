# 会话地图

本文件是 nexcore 的唯一会话台账（主规范 §9.5 派发三要素的落点）。任务级明细写在 phase 卡里，本文件只记「哪个任务落在哪个对话」。

## 对话清单

| 类型 | 名称 | 负责 | 产物 | 状态 |
| --- | --- | --- | --- | --- |
| 常驻 | `[nexcore] 需求池` | REQ | `issues/` 台账、phase 立项 | 待建 |
| 常驻 | `[nexcore] 技术池` | ARCH | `docs/design.md`、battle 记录 | 待建 |
| 常驻 | `[nexcore] 评审` | REVIEW | 复核结论、`verified` 证据 | 待建 |
| phase | `[nexcore] P<nn> · <目标一句话>` | DEV / VERIFY | `docs/phases/PH-<n>/card.md` 与收口文档 | 尚无一期（按 §9.3 命名，两位补零） |

叙事：三类常驻对话与首期 phase 对话都还没有创建。在它们建起来之前，本仓库不满足主规范 §3.1 的「角色落在对话里」，任何实现提交都缺上游主张权。

## 任务-对话映射

| 任务 | 对话 | 角色 | phase |
| --- | --- | --- | --- |
| （暂无已派发任务） | — | — | — |

首期任务在 `PH-01` 卡冻结后回填本表；回填前不得开工。

## 门禁入口

唯一入口：`bash tools/check.sh`。当前已实现的只是规范门禁的子集，如实登记如下。

| 命令 | 做什么 | 状态 |
| --- | --- | --- |
| `bash tools/check.sh g0` | 规约纳管：生成块头 `version`/`sha256`、全文副本 sha256、块体与 `sync-agents.sh --print` 逐字、块外 0.1/0.2/0.3 三段、实例化 8 行、`docs/session-map.md`、版本落点 | 已实现 |
| `bash tools/check.sh g1` … `g9` | 主规范 §6.4 其余门禁 | 未实现（调 `g1`..`g9` 一律退出码 2） |
| `bash tools/check.sh`（不带参数） | 构建 + 机械检查全量：`0 / ①a ①b ①c / ②a ②b ②c / ③ / ④ / ⑤a ⑤b / ⑥ / ⑦ / ⑧ / ⑨ / smoke` | 已实现，末行 `ALL CHECKS PASSED` |

全量检查的环境前提：`ct` 在 PATH 上（本机在 `~/.local/bin/ct`，默认 PATH 不含）、`python3`、`cc`，以及 `experiments/third_party/quickjs/libquickjs.a`（`.gitignore` 忽略，由 `experiments/build.sh` 拉取构建）。`tools/check.sh` 不读 `CT_STD_DIR`。

## 版本落点

两套版本号不混用（主规范 §6.7 / §11.4）：产品版本归 `docs/README.md`，规范版本归 `AGENTS.md` 生成块头。

| 版本 | 位置 | 当前值 |
| --- | --- | --- |
| 产品版本 | `docs/README.md` 的「- 状态：v0.13.0（…）」一行 | `v0.13.0` |
| 规范版本 | `AGENTS.md` 生成块头 `<!-- spec:begin version=… sha256=… -->` | `1.10.0` / `e3905e9bf8637a1e486a8498ed653628ec3c50be53298c60e609efc3b6a33caf` |
| 规范全文副本 | `docs/spec/ai-coding-workflow.md` | sha256 必须等于上方块头 |

## 注记

- **CT 基线（2026-11-15 已重定，最终口径）**：`docs/README.md:19`、`docs/tutorial.md:6,98`、`experiments/README.md:4`、`experiments/build.sh:5`、`docs/design.md:250`、`docs/acceptance.md:46`、`docs/open-questions.md:291,295` 已统一为 `ct v0.44.2`（`ct-lang` tag `v0.44.2` = `328e5e63`）。原 `c27da3d`（v0.19.1）作废；`ct v0.44.1` 只是同日的中间态，不构成基线。`reviews/`、`2026-11-12/` 下的旧版本号属带日期的历史记录，不代表现行基线。
- **门禁覆盖缺口**：`tools/check.sh` 目前只有全量检查与 `g0`，`g1`..`g9` 未实现；`docs/session-map.md` 是本轮新建，`issues/`、`docs/phases/`、`docs/snippets/`、`docs/decisions/` 仍不存在。
- **纳管状态**：nexcore 已纳入主规范 §11.5 L-7 的纳管清单（清单行登记时 `spec-version` 1.9.0，本轮同批升到 1.10.0）；`AGENTS.md` 生成块与 `docs/spec` 全文副本均已刷新到 1.10.0，`tools/check.sh g0` 全覆盖，L-3 的「未纳管」判定解除。
