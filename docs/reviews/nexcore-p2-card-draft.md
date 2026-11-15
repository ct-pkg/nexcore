# P2 卡草案：把 QuickJS 实现接进 `js_backend` 接口

> 状态：**已施工**（2026-11-15）。上游：`docs/decisions.md` D-21、`docs/reviews/nexcore-p1-quickjs-ruling.md`。
> 本卡只写判据、前置与方案；不新增代码、不改 `docs/roadmap.md` 与验收条目。

## 1. 目标

让 `NEXCORE_JS=quickjs` 成为**真能跑**的档位：同一个 `js_backend` 接口在 quickjs 档指向 QuickJS 实现，⑧ `bootstrap_js executed`、⑨ `drain → sink`、`smoke` 三条从 SKIP 回到绿线；null 档保持现在的行为不变。

## 2. 前置（缺一条都开不了工）

| # | 前置 | 现状 |
| --- | --- | --- |
| 1 | `experiments/third_party/quickjs/libquickjs.a` 可构建 | 已有（`experiments/build.sh` 默认档全 PASS） |
| 2 | P1 的 null 档全绿 | 已有（`ALL CHECKS PASSED (NEXCORE_JS=null)`） |
| 3 | `js_backend` 接口与映射稳定 | 已有（`src/js_backend/js_backend_map.m.ct` 映射 4 个方法） |
| 4 | 允许在 `src/js_backend/` 下新增第二个实现文件 | 无政策阻碍，但需评审确认「同目录双实现」的命名约定 |

## 3. 为什么不能只靠环境变量/构建参数

这一点必须先说清楚，否则会写出跑不动的方案。CT 侧有两条硬约束（均为实测原文）：

1. **`namespace` 必须等于目录名。** 把实现文件放到别的目录会直接失败：
   `ctc: namespace 'js_host_null' does not match the directory 'js_host'`。
2. **实现单元的第一个 import 必须是自己的 `.h.ct`，且名字与文件名绑定。** 放到同目录但改名会红在 ⑤a：
   `⑤a FAIL: 实现单元首段 import 必须是 import js_host_null;` / `只允许自身`。

推论：**不能靠一个 `-D` 或一个环境变量在同一份源码里切实现**（CT 没有 `#if`，`import` 是硬编码源码文本），也不能让一份 map 指向两套符号。可行的最小代价是「同一命名空间、同一目录下放两个实现文件，构建时只编其中一个」。

> 更正一条早先的判断：裁定文档 §7 曾把「同命名空间在构建期指向两套实现」记为不可行。更精确的结论是——**跨目录不可行，同目录可行**。本卡 §4 的第 2 条已实测证实。

## 4. 方案（已实测骨架）

### 4.1 实测证据

| # | 动作 | 结果 |
| --- | --- | --- |
| 1 | 新增 `src/js_backend/js_backend_quickjs.ct`（namespace `js_backend`，首行 `import js_backend_quickjs;`）+ 同名 `.h.ct`，**不注册进 `units.json`**，跑 null 档全量 | rc=0，`ALL CHECKS PASSED (NEXCORE_JS=null)`。⓪ ④ ⑤a ⑤b 都不因这个「未注册的兄弟文件」而红 |
| 2 | `ct build --ns-root src --emit-c src/js_backend/js_backend_quickjs.ct -o out` | rc=0，产出 `js_backend_quickjs.c/.h` |
| 3 | `cc -std=c99 -Wall -Wextra -Werror -isystem <quickjs> -fsyntax-only` 上述产物 | rc=0，零警告 |
| 4 | 比对导出符号与 map 目标 | 实现给出 `js_backend_eval/shutdown/turns/init`，与 `js_backend_map.m.ct:16-19` 的映射目标逐字一致 |

### 4.2 工作项

| # | 文件 | 改动 | 判据 |
| --- | --- | --- | --- |
| 1 | `src/js_backend/js_backend_quickjs.h.ct`（新） | 与 `js_backend.h.ct` 同签名，`pub` 4 方法 | ⑤b 不把它当注册单元校验；`ct build` 通过 |
| 2 | `src/js_backend/js_backend_quickjs.ct`（新） | 由 `src/js_host/js_host.ct` 迁移，改 `namespace js_backend;` 与自 import | ④ 通过；导出符号与 map 一致（已实测） |
| 3 | `tools/check.sh` | 恢复档位逻辑：`null` → `IMPL_UNITS` 用 `js_backend/js_backend`；`quickjs` → 用 `js_backend/js_backend_quickjs`，并加回 `-isystem "$QJS"` 与 `"$QJS/libquickjs.a"`；⑧⑨/smoke 三节在 quickjs 档真跑 | `NEXCORE_JS=null` 与 `NEXCORE_JS=quickjs` 两档都 rc=0，末行分别带档位 |
| 4 | `src/js_host/*` | 迁移完成且两档稳定后删除，避免两处 QuickJS 代码漂移（删除属独立提交，可回滚） | `grep -r js_host src/` 仅剩历史注释 |
| 5 | `experiments/build.sh` | 保持现状（默认 quickjs），null 档 SKIP | 两档 rc=0 |

**不需要动的东西**：`units.json`、`js_backend_map.m.ct`、`src/NexCore.ct`、公开 ABI 头。接口与调用点在 P1 已经定死，这正是 D-21 说的「P2 只换实现文件」。

## 5. 判据（本卡完成 = 全满足）

1. `NEXCORE_JS=quickjs bash tools/check.sh` → rc=0，且 ⑧⑨/smoke **实际执行并 PASS**（日志里不能再出现 `SKIP`）。
2. `NEXCORE_JS=null bash tools/check.sh` → rc=0，行为与 D-21 收口时一致（⑧⑨/smoke 仍是 SKIP，不计通过）。
3. `bash experiments/build.sh` 与 `NEXCORE_JS=null bash experiments/build.sh` 均 rc=0。
4. 两档产出的 `nexcore_*` 导出符号集合完全相同（②c 的比对逻辑复用）。
5. `bash tools/check.sh g0` rc=0。

## 6. 退路（接线失败时怎么回 null）

- 单文件级：`tools/check.sh` 的 quickjs 分支加回 `exit 2`（即现状），实现文件留在 `src/js_backend/` 不影响任何档位——已实测未注册的兄弟文件不会弄红 null 档，所以**回退成本是零代码删除**。
- 提交级：本次接线单独成一个提交；失败时 revert 该提交即可，不牵动 P1 的 `js_backend` 接口与 `units.json`。
- 不再需要的动作：不需要重建 `third_party/`，不需要改 ABI，不需要回滚 CT 基线。

## 7. 风险

| 风险 | 应对 |
| --- | --- |
| 同目录双实现被误解为「两个单元」 | 在 `units.json` 里只注册 `js_backend`；在 `js_backend_quickjs.h.ct` 顶部注明「非注册单元，仅 quickjs 档使用」 |
| 迁移后 `src/js_host/*` 与 `js_backend_quickjs.ct` 两处 QuickJS 代码漂移 | 先迁移 + 两档稳定，再删旧目录（工作项 4 单列提交） |
| quickjs 档的 ⑧⑨ 通过依赖真实引擎，CI 环境可能没有静态库 | quickjs 档先做「本地必跑」，CI 待静态库缓存方案确定后再纳入；在此之前不得把 quickjs 档写成 P1 判据 |
| 双档位让「全绿」语义再次模糊 | 两档末行都必须带档位字符串；文档统一用 `ALL CHECKS PASSED (NEXCORE_JS=…)` 表述 |

## 8. 明确不做

- 不改 `docs/roadmap.md` 的退出判据、不改验收条目、不动 `.ai-engineering`。
- 不把 QuickJS 重新登记成独立单元（`units.json` 只保留 `js_backend`）。
- 不在本卡恢复 `js_host_map.m.ct`（那是 D-21 明确删掉的生成物）。
- 不顺便重构 `experiments/build.sh` 的探针结构。

---

## 9. 实施记录（2026-11-15）

两条 CT 约束已固化进代码注释，不再只活在对话或文档里：

- `src/js_backend/js_backend_quickjs.h.ct` 顶部注明「namespace 必须等于目录名」「首段 import 必须是自己 .h.ct」，并写明因此第二实现只能同目录；
- `src/js_backend/js_backend_quickjs.ct` 顶部重复同一组约束，并说明「null 档编 `js_backend.ct`，quickjs 档编本文件」；
- `tools/check.sh` 的档位注释同样记录这两条，防止后来人改回「换目录」方案。

实测（副本，`ct v0.44.2`，QuickJS 静态库复用）：

| 命令 | 结果 |
| --- | --- |
| `NEXCORE_JS=quickjs bash tools/check.sh` | rc=0，`ALL CHECKS PASSED (NEXCORE_JS=quickjs)`；⑧ `PASS`、⑨ `PASS`、smoke 实际执行（无 SKIP） |
| `bash tools/check.sh`（null 档） | rc=0，`ALL CHECKS PASSED (NEXCORE_JS=null)`；⑧⑨/smoke 仍是 SKIP 且不计通过 |
| 零删除回退：给 quickjs 分支加回 `exit 2` | null 档 rc=0 不变；quickjs 档 rc=2 打印拒绝原因；复原后 quickjs 档回到 rc=0 |

**过程中补的一处**：②a 现在先单独生成 `js_backend.h`（接口头），再生成实现单元。原因是 quickjs 档的实现文件只带自己的 `js_backend_quickjs.h.ct`，不会产出 `js_backend.h`，而 `NexCore.c` 里 `import js_backend::js_backend_map` 需要它。顺序必须先头后单元，否则第二次生成会把实现 `.c` 覆盖成空文件，链接时报 `_js_backend_init` 未定义。

**未做**：卡里工作项 4（删除 `src/js_host/*`）按「稳定一轮后再删」保留，当前 `src/js_host/` 与 `js_backend_quickjs.ct` 两处代码并存，尚未漂移。
