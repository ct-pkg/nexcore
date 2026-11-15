# 裁定：P1 不依赖 QuickJS，引擎接口在 P1 定死、QuickJS 归 P2

> 状态：**已裁定**（2026-11-15）。对应 `docs/decisions.md` 的 D-21。
> 上游材料：`docs/reviews/nexcore-p1-quickjs-battle.md`（两席，未裁）；本文补完两席的空白项并给出判据。
> 约束：不改 `docs/roadmap.md` 的 P1 退出判据（冻结），不改 `.ai-engineering`。

---

## 1. 裁定

**P1 的构建不引入 QuickJS。** 具体三条：

1. `src/NexCore.ct` 里对 `js_host` 的 5 处调用（`import` 1 处、`init` / `eval` / `shutdown` 3 处、共 5 个调用点）改为经 P1 自有的空后端接口 `js_backend`；命名空间从 P1 起固定为 `js_backend`，P2 只换实现文件，不改调用点。
2. QuickJS 依赖整体后移到 P2（与 `docs/roadmap.md:13` 的分期一致）；`experiments/build.sh` 的引擎探针保留，标为 P2 资产。
3. `tools/check.sh` 增加档位：默认 `NEXCORE_JS=null`（P1 出口用它），`quickjs` 档保留现有 ⑧⑨/smoke 的 JS 断言供 P2 回归。

**Q-01 同时收口**：P2 的后端定为 Bellard QuickJS `2025-09-13`（现仓库唯一实测通过的组合）；换 fork 的迁移面就是 `js_backend` 一个单元，Q-01 从「待定」降为「已选，换 fork 时重开」。

---

## 2. 两席（对打结论，不是罗列）

### 主张席：认下依赖

- **构建**：`tools/check.sh` 已有 6 处 `-isystem "$QJS"`、4 处链接 `libquickjs.a`；`experiments/build.sh` 有 3 处实体引用。认下依赖后这些行一行不改。
- **依赖与可复现**：`third_party/quickjs` 被 `.gitignore` 忽略，靠 `build.sh` 现场拉 `QJS_VERSION=2025-09-13`，**没有任何摘要校验**——这是当前最大的洞：同版本号不同内容也能过。
- **可分发性**：公开 ABI 头零引擎符号（实测），所以「引不引引擎」不影响宿主看到什么；影响的是 `libnexcore` 的构建前提与产物体积。
- **代价**：`js_host` 的 10 个引擎符号成为 P1 构建的硬前提；P1 出口判据「没有 QuickJS 依赖也能构建」（`docs/roadmap.md:55`）当场失效，而该判据冻结、本裁定不得改。

### 反证席：必须不依赖

- **解耦方式**：新增 `js_backend` 单元（接口 4 个方法：`init` / `eval` / `shutdown` / `turns`），P1 提供空实现，`src/NexCore.ct` 只依赖接口。
- **动多少代码**：`src/NexCore.ct` 5 个调用点改名；`units.json` 加 1 条；`tools/check.sh` 的 `IMPL_UNITS` 改 1 行、6 处 `-isystem` 与 4 处链接改条件化；`experiments/build.sh` 的 QuickJS 段落加「可选」守卫。
- **会不会把 `create` 的心智模型搞复杂**：不会。`create` 仍是「分配→开队列→起后端→置状态」，只是 `js_host::Host.init` 变 `js_backend::Backend.init`，失败语义不变（返回 0 → `NX_E_INTERNAL`）。
- **代价**：P1 交付的「JS 可 eval」能力消失，⑧⑨/smoke 三条检查从 P1 绿线上移走；`docs/acceptance.md` 里依赖 JS 线程的 ACC-C-01/05/06 本来就写在 P2 关闭（`docs/roadmap.md:53`），与本次移动自洽。

### 对打结论

主张席的唯一致命伤是**它必须改冻结判据才能成立**；反证席的代价是**可枚举、可实测的机械改动**。成本不对称，取反证席。

---

## 3. 实测证据（本机 `ct v0.44.2`，仓库副本）

| # | 做法 | 结果 | 说明 |
| --- | --- | --- | --- |
| 1 | 把 `src/js_host/js_host.ct` 换成空实现（`init` 返回非空句柄、`eval` 恒 `-1`），**删掉** `experiments/third_party/`，跑 `tools/check.sh` | ①②③④⑤ 全过；⑥ 起报 `clang: error: no such file or directory: 'experiments/third_party/quickjs/libquickjs.a'` | 证明「无引擎也能过全部代码类检查」，红点只在测试链接行，不在产品代码 |
| 2 | 同上，但保留 QuickJS | ①②③④⑤ 全过，⑥⑦⑧⑨ 报语义失败（`eval` 恒 -1） | 证明失败面只集中在 JS 语义检查，不是构建 |
| 3 | 把后端放成 `src/js_host/js_host_null.ct`（namespace `js_host_null`） | `ctc: namespace 'js_host_null' does not match the directory 'js_host'` | CT 硬约束：**namespace 必须等于目录名**，所以「同目录换文件」不可行，必须独立目录 |
| 4 | 同上但文件内改为 `import js_host;` | ⑤a 报两处：自 import 必须是 `import js_host_null;`、且不得 import 其他单元 | 生成器/⑤a 的单元命名与文件名绑定，第二后端要走正式注册 |
| 5 | 公开 ABI 头是否含引擎符号（沿用 battle 文档 §3） | 0 个引擎符号 | 依赖面不影响宿主 API |

第 1、3、4 条是本次新增，直接改变了方案的形状：**不是「加个开关」，而是「新增一个正式注册的 `js_backend` 单元」**。这也是为什么裁定不给「可选特性」路线——CT 没有 `#if`，可选只能靠构建期选单元，而选单元就必然要过 ⑤a/⑤b 的注册校验。

---

## 4. 执行清单（可核对）

| # | 文件 | 改动 | 判据 |
| --- | --- | --- | --- |
| 1 | `src/js_backend/js_backend.h.ct`（新） | `pub` 声明 4 个方法，语义与现 `js_host.h.ct` 相同 | `ct build --emit-c` 通过；④ recipe 校验通过 |
| 2 | `src/js_backend/js_backend.ct`（新） | 空实现：`init` 返回堆句柄、`eval` 返回 `-1` 且不写 `*out`、`shutdown` free、`turns` 返回 0 | ⑥⑦ 的语义断言不依赖它；本条已在 §3 第 1 项以等价形态实测 |
| 3 | `src/units.json` | 移除 `js_host` 条目、新增 `js_backend` 条目；同时删掉 `src/js_host/js_host_map.m.ct`（不删则 ④ 因 `js_host_init` 不在本次生成的导出原型里而红） | ⓪ generator drift 通过；⑤b 通过；④ 通过 |
| 4 | `src/js_backend/js_backend_map.m.ct`（新，生成物） | 由 `tools/nexcore_gen.py` 生成，映射 `js_backend::Backend.*` → `js_backend_*` | 生成物与 `--check` 一致 |
| 5 | `src/NexCore.ct` | `import js_host::js_host_map` → `import js_backend::js_backend_map`；5 个调用点改名 | ②a/②b/②c 通过；`nexcore_*` 7 个 T 符号不变 |
| 6 | `tools/check.sh` | `IMPL_UNITS` 改 `js_backend/js_backend`；默认 `NEXCORE_JS=null`，⑧⑨/smoke 记 SKIP；`quickjs` 档暂未接线，直接退出码 2（见 §7） | `bash tools/check.sh` 全绿且末行带档位；`NEXCORE_JS=quickjs` rc=2 并打印原因 |
| 7 | `experiments/build.sh` | 新增 `JS_BACKEND="${NEXCORE_JS:-quickjs}"`；`null` 档跳过引擎拉取与 bridge 探针（打印 SKIP） | 两档都 `rc=0`：null 档 4 PASS + 1 SKIP；quickjs 档 5 项全 PASS |
| 8 | `src/js_host/*` | **保留不动**（只留 `js_host.ct` / `js_host.h.ct`，不再进构建面），作为 P2 的 QuickJS 实现，接线时按本表重新注册 | P2 才动；P1 不删，避免丢能力 |

**P1 出口的证明方式**：`NEXCORE_JS=null bash tools/check.sh` 全绿 + `bash tools/check.sh g0` 全绿 + `otool -L` 产出的 `libnexcore` 只含 libc/pthread。第三条未实测（当前产物是目标文件，不是 dylib），标 `[未实测]`。

---

## 5. 主动性风险与留退路

| 风险 | 留退路 |
| --- | --- |
| P1 失去 JS 能力，早期集成方无法提前验证桥接 | `NEXCORE_JS=quickjs` 档位保留，随时可跑；P2 只需换实现文件，不改 ABI/调用点 |
| 空后端让 ⑧⑨/smoke 不再守 P1，回归覆盖变薄 | 把三条检查登记为 P2 的必过项（不改 `roadmap.md`，在 P2 卡里写明），并在 P1 期间由 `quickjs` 档手工跑 |
| 新增 `js_backend` 与现 `js_host` 并存造成「两个后端」歧义 | 命名上区分：`js_backend` 是接口，`js_host` 明确标注为「P2 的 QuickJS 实现」；P2 落地时把 `js_host` 移入 `js_backend_quickjs/` 并删除旧命名 |
| 依赖获取仍不可复现（`build.sh` 无摘要校验） | P2 开工前给 `QJS_VERSION` 配 `QJS_SHA256` 并在下载后校验（`shasum -a 256 -c`，失败即红）。摘要值需现场量一次，标 `[未实测]` |

---

## 6. 明确不做的事

- 不改 `docs/roadmap.md` 的 P1 退出判据（冻结），只按它执行。
- 不删 `src/js_host/`、不删 `tests/`、不删 `experiments/` 的 QuickJS 探针，也不删 `experiments/third_party/`。
- 不动 `.ai-engineering`。
- 不在本文改 `docs/acceptance.md` 的 ACC 条目；依赖 JS 线程的三条（ACC-C-01/05/06）已由 `docs/roadmap.md:53` 写明在 P2 关闭，无需改写。

---

## 7. 实施记录（2026-11-15，8 步已落地）

| 实测项 | 命令 | 结果 |
| --- | --- | --- |
| P1 档全量 | `bash tools/check.sh` | rc=0，末行 `ALL CHECKS PASSED (NEXCORE_JS=null)`；① ②a ②b ②c ③ ④ ⑤a ⑤b ⑥ ⑦ 全部真跑 |
| JS 语义三条 | 同上 | ⑧⑨/smoke 打印 `SKIP (… 不计通过)`，**没有计入绿线** |
| P2 档 | `NEXCORE_JS=quickjs bash tools/check.sh` | rc=2，打印「尚未接线」及原因，不装绿 |
| 地基 null 档 | `NEXCORE_JS=null bash experiments/build.sh` | rc=0；第 1 节 SKIP，②③④⑤ 四项 PASS |
| 地基 quickjs 档 | `bash experiments/build.sh` | rc=0；5 项全 PASS（含 bridge 的 7 条断言） |
| 规约门禁 | `bash tools/check.sh g0` | rc=0，`ok g0 (block 1.9.0/b8651332, …)` |

**与原计划的偏离（一处）**：原计划让 `quickjs` 档直接跑 ⑧⑨。实施中发现 CT 的两条硬约束——`namespace` 必须等于目录名、`units.json` 与 `*_map.m.ct` 一一对应——使得「同一接口命名空间 `js_backend` 在 quickjs 档指向 `js_host_*` 实现」无法靠构建参数完成，必须先做一层显式映射/注册，属 P2 的接线工作。因此 `quickjs` 档改为**明确拒绝（rc=2）**，而不是让它带着空后端跑出假红或假绿。桥接能力在 P2 接线完成前由 `experiments/build.sh` 覆盖（默认档仍跑满 5 项）。

`experiments/third_party/` 与 `src/js_host/` 源码均保留，P2 接线时不需要重新拉取或重写实现。
