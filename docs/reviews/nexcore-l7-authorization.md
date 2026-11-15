# 待授权：主规范 §11.5 L-7 收录 nexcore

> 状态：**未执行**。本文只是一份可直接照做的变更单，`.ai-engineering` 一个字节都没动。
> 依据：`/Users/samxx/trae-ws/.ai-engineering/methodology/ai-coding-workflow.md:1226-1233`（L-7 清单与占位规则）、`:1159-1170`（11.4 防漂移与升级）。

---

## 1. 变更内容（只有一行）

在 L-7 纳管清单表格里新增 nexcore 一行。清单当前只有 `ct-lang`、`sam-utils` 两行，且状态均为 `pending（未落库）`。

### 插入位置

`methodology/ai-coding-workflow.md:1230` 之后、`:1231`（sam-utils 行）之前——按字母序落在 `ct-lang` 与 `sam-utils` 之间；若你希望新行一律追加到表尾，则插在 `:1231` 之后。

插入锚点原文（`ct-lang` 行）：

```
| ct-lang | `/Users/samxx/trae-ws/ct-lang` | `<待项目侧会话回填>` | `<待回填，非已确认>` | `<待回填，非已确认>` | `pending（未落库）` |
```

### 方案 A：实测回填版（推荐，四列已有可核对值）

```
| nexcore | `/Users/samxx/trae-ws/nexcore` | `AGENTS.md`（生成块 1.9.0 / b8651332…，已落库） | `1.9.0` | `2026-11-15`（提交 `7a5c2a1`） | `active` |
```

四列的实测依据（均可复跑）：

| 列 | 值 | 判据命令 |
| --- | --- | --- |
| `AGENTS.md` | 存在且含生成块 | `grep -c '^<!-- spec:begin version=1.9.0' AGENTS.md` → 1 |
| 块头 version | `1.9.0` | `grep -m1 '^<!-- spec:begin' AGENTS.md` |
| 最近同步 | `2026-11-15` / `7a5c2a1` | `git -C /Users/samxx/trae-ws/nexcore log -1 --format='%ad %h' --date=short -- AGENTS.md docs/spec` |
| 状态 | `active`（已落库，`g0` 绿） | `bash tools/check.sh g0` → `ok g0 (block 1.9.0/b8651332, …)` |

### 方案 B：占位版（严格照 L-7 现有体例，四列交给项目侧回填）

```
| nexcore | `/Users/samxx/trae-ws/nexcore` | `<待项目侧会话回填>` | `<待回填，非已确认>` | `<待回填，非已确认>` | `pending（未落库）` |
```

差别只在「这一行落地时是否直接算已确认」。方案 B 会多一轮往返；方案 A 的每个值都能当场核对，我建议 A。

---

## 2. 连带影响（授权前必须知道）

1. **11 个仓库的 pin 会同时失效**。项目 `AGENTS.md` 的块头 `sha256` 与 `docs/spec/ai-coding-workflow.md` 的实算值都等于主规范全文的 sha256；主规范加一行 → sha 变 → 所有已纳管项目的 `g0`/`G9` 立即变红，直到逐个 `sync-agents.sh --write` 并提交。
2. **`spec-version` 必须升位**。按 11.4「新增或修改条款 → MINOR +1」，应从 `1.9.0` 升到 `1.10.0`；该规则同时要求「主规范头部版本与 git tag 一致」，所以 `.ai-engineering` 侧要打 `v1.10.0` tag。这一步属于条款变更，不是纯措辞。
3. **升级块要单独成提交**。11.4 明确「该提交只改 `AGENTS.md`（生成块），不夹带其他文件」；nexcore 侧不能把块刷新和业务改动混在一次提交里。
4. **不授权的后果**：L-7 是「可枚举、唯一一份」的清单，清单之外即使有 `AGENTS.md` 也不算纳管（`:1233`），按 L-3（`:1191`）nexcore 的 G0 永远只能判「未纳管」，G1 也不得开新 phase。

---

## 3. 授权后 nexcore 侧还要补什么

按顺序，四步：

| # | 动作 | 具体位置 | 判据 |
| --- | --- | --- | --- |
| 1 | 刷新生成块与全文副本 | `sync-agents.sh --write AGENTS.md`，产生 `AGENTS.md` + `docs/spec/ai-coding-workflow.md` 两文件变更 | 块头 `version`/`sha256` 与新主规范一致；`--print` 与块体逐字相同 |
| 2 | 单独提交块升级 | 只含上述两文件，提交信息形如 `docs(spec): 升级工程规范 1.10.0` | 11.4「升级块单独成提交」 |
| 3 | 改 `docs/session-map.md` 的纳管状态 | `docs/session-map.md:50` 现写「nexcore 尚未进入主规范 §11.5 L-7 的纳管清单，按 L-3 仍判「未纳管」」 | 改为「已纳入 L-7（`spec-version` 1.10.0），G0 全覆盖」 |
| 4 | 复跑并留证 | `bash tools/check.sh g0` + 全量 | `g0` 退出码 0；全量末行 `ALL CHECKS PASSED` |

另外两处可选（不阻塞）：

- `docs/reviews/nexcore-readiness.md` §6 第 1 条「推动 L-7 收录」可标已闭合；
- `docs/reviews/nexcore-status.md` §4 G0 行的「缺主规范 L-7 收录」可改为已具备，§7 表第 1 行同改。

---

## 4. 执行顺序建议（跨两个仓库）

1. `.ai-engineering`：改主规范一行 + 头部 `spec-version` 1.10.0 + 提交 + 打 `v1.10.0` tag（**需你点头**）。
2. 逐仓库 `sync-agents.sh --write`（11 个仓库，各自单独提交）。
3. nexcore：更新 `docs/session-map.md` 纳管状态，复跑 `g0` 与全量。
4. 其余仓库的门禁末行一并回填到本清单的表格里，避免「纳管了但没证据」。

> 提醒：第 1 步一旦执行，在 11 个仓库全部重新同步之前，任何一处 `g0`/`G9` 都会红。这是设计如此，不是回归。
