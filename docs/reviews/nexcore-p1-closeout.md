# P1 收口记录

> 日期：2026-11-15 ｜ 基线：`ct v0.44.2`（`328e5e63`）｜ 提交：`834736a`（D-21 实施）
> 关联：`docs/reviews/nexcore-p1-quickjs-ruling.md`（裁定 + 实施记录）、`docs/decisions.md` D-21、`docs/reviews/nexcore-p2-card-draft.md`（下一步）

## 0. 先写清楚一句话

**P1 的绿线不包含 JS 语义检查。** `bash tools/check.sh` 通过，只代表 ABI、单元规约、统计键、队列这些**不依赖 JS 引擎**的部分通过；`bootstrap_js executed`、`drain → sink`、`smoke` 三条依赖 JS 求值的检查在 P1 档位打印 `SKIP`，**不计入通过**。谁引用「P1 全绿」时，必须同时说明这三条没跑。

## 1. 退出判据逐条对账

依据 `docs/roadmap.md:53-57`，五条逐条对：

| # | 判据原文（摘要） | 状态 | 证据 / 缺口 |
| --- | --- | --- | --- |
| 1 | ACC-A-01..03、05、06、08..10 通过；ACC-C-02、C-08、C-09（基础部分）通过；ACC-C-01/05/06 于 P2 关闭 | **部分（新增 ⑩ 后前进一格）** | ACC-A-01 由 ① 真跑；ACC-A-02 由新增 ⑩ 覆盖（`nm -gU` 恰好 7 个 `nexcore_*`）；ACC-A-12 由 ⑥ 真跑；ACC-C-08/C-09 基础由 ⑦ 与 handle 单测覆盖；ACC-A-05 Swift 冒烟由 `experiments/build.sh` 第 2 节 PASS。**仍缺**：ACC-A-07（Android NDK）、ACC-A-06（Kotlin/JVM）**本机不可验证**，登记而非跳过；ACC-A-04/08/09/10/11 依赖假时钟注入与 golden 文件，尚未建测例 → `[未验证]` |
| 2 | Q-06（Swift/Android 集成形态与最低版本）在 P1 退出前关闭 | **已闭合（DEFAULTED）** | 2026-11-15 按临时默认关闭，依据写在 `docs/open-questions.md:136-142`：Swift 形态由 `swiftc -import-objc-header` 冒烟实测 PASS；Android 取 minSdk 26 / 独立 JNI `.so`，**判为不可验证**；Swift 6 严格并发留 P4 前 |
| 3 | 没有 QuickJS 依赖也能构建；`libnexcore` 依赖只有 libc/pthread | **已闭合（构建面 + 产物面）** | `NEXCORE_JS=null bash tools/check.sh` rc=0；新增 ⑩ 真产出 `build/bin/libnexcore.dylib`，`otool -L` 只有 `libSystem.B.dylib`，无 quickjs；`nm -gU` 7 个 `nexcore_*` |
| 4 | 所有 `.ct` 通过 `check-boundaries.sh`（无 `#if`、无 `errno`、import 顺序正确） | **已闭合** | 新增 `tools/check-boundaries.sh`，并在 `check.sh` 里作为 ⑤c 调用：17 个 `.ct` 全过；注入 `#ifdef`、`errno`、import 后置三类违规在副本上均红（红证见 §3） |
| 5 | `NexCoreConfig` 的 `_Static_assert` 在 macOS/Android 两工具链都通过 | **一半（Android 不可验证）** | macOS 侧由 ① 与 `experiments/build.sh` 第 2/3 节真跑；Android 侧无 NDK 工具链 → **登记为不可验证**，不计通过 |

**结论**：五条判据里 **3 条已闭合**（Q-06、不依赖 QuickJS、boundaries），2 条**因环境不可验证**（Android NDK、Kotlin/JVM）如实登记、不计通过。P1 的机械防线现在覆盖到产物面：`⑩` 会拦住「某天有人把 quickjs 链进 libnexcore」这类回归。

## 2. 三处处置的账

### 2.1 ⑧⑨/smoke 三条 SKIP

| 检查 | 依赖什么 | P1 档表现 | 转移到哪 |
| --- | --- | --- | --- |
| ⑧ `bootstrap_js executed` | `bootstrap_js` 被 JS 引擎求值 | `SKIP (NEXCORE_JS=null：空后端不求值，不计通过)` | P2，`quickjs` 档（待接线，见 P2 卡草案） |
| ⑨ `drain → sink` | JS 处理消息后产出响应，经 outbox 到 sink | `SKIP (…空后端不产出响应，不计通过)` | 同上 |
| `smoke` | 端到端跨单元调用链含 JS 求值 | `SKIP (…端到端依赖 JS 求值，不计通过)` | 同上 |

三条 SKIP 都带原因且不计通过，`check.sh` 末行显式带档位：`ALL CHECKS PASSED (NEXCORE_JS=null)`。

### 2.2 `js_host` 退出注册表

- **改了什么**：`src/units.json` 移除 `js_host` 条目；删除生成物 `src/js_host/js_host_map.m.ct`；`src/js_host/js_host.ct`、`js_host.h.ct` 源码保留。
- **为什么必须移**：`④`（recipe callee 校验）按 `units.json` 的注册单元生成导出原型再比对调用点。`js_host` 还注册着但实现不在 P1 构建清单里时，`js_host_init` 不会出现在生成头中，④必然红。这不是风格问题，是机械门禁的必然结果。
- **影响**：P1 构建面不再包含任何 QuickJS 符号；`js_host` 从「已注册单元」降为「P2 待注册的实现」。P2 接线时必须重新登记并补回映射文件，不能只改 `IMPL_UNITS`。

### 2.3 `quickjs` 档 `rc=2`

- **改了什么**：`NEXCORE_JS=quickjs bash tools/check.sh` 直接退出码 2，打印未接线原因。
- **为什么不放它跑**：当前 `NEXCORE_JS` 只控制「是否链接引擎」，而实现单元固定是空后端 `js_backend`。让 quickjs 档跑下去，⑧⑨/smoke 会因为 `eval` 恒 `-1` 而红——看起来像回归，实际是接线没做。按仓库既有约定「不能跑的门禁不许装绿」，宁可直接拒绝也不产出误导性的红/绿。
- **影响**：P1 期间「QuickJS 能跑通」的唯一证据是 `experiments/build.sh`（默认档 5 项全 PASS，含 bridge 的 7 条断言），不是 `check.sh`。P2 接线路径已实测（同目录第二实现可行），见 `docs/reviews/nexcore-p2-card-draft.md`；接线完成前，`rc=2` 是本档的安全默认。
- **后续（2026-11-15 P2 施工后）**：该限制已解除。`NEXCORE_JS=quickjs bash tools/check.sh` 现在 rc=0，⑧⑨/smoke 真跑 PASS；null 档保持 SKIP 语义不变。本节保留的是 P1 收口当时的状态记录。

## 3. 本次收口的机械证据

| 命令 | 结果 |
| --- | --- |
| `bash tools/check.sh` | rc=0，`ALL CHECKS PASSED (NEXCORE_JS=null)`，real 2.94s → 新增 ⑤c/⑩ 后仍 rc=0 |
| `NEXCORE_JS=quickjs bash tools/check.sh` | rc=2，打印未接线原因 |
| `NEXCORE_JS=null bash experiments/build.sh` | rc=0；第 1 节 SKIP，②③④⑤ PASS |
| `bash experiments/build.sh` | rc=0；5 项全 PASS |
| `bash tools/check.sh g0` | rc=0，`ok g0 (block 1.9.0/b8651332, …)` |
| `bash tools/check-boundaries.sh` | `boundaries: PASS（17 个文件，无 #if / 无 errno / import 顺序正确）`，rc=0 |
| 红证：副本里给一个 `.ct` 注入 `#ifdef FOO` + `errno` + 尾部 `import` | `src/x/bad.ct:55: 禁止条件编译：#ifdef FOO` / `:58: 禁止使用 errno` / `:59: import 出现在声明之后`，`FAIL（3 处违规）`，rc=1 |
| `otool -L build/bin/libnexcore.dylib` | 只有 `libSystem.B.dylib`，无 quickjs |
| `nm -gU build/bin/libnexcore.dylib` | 恰好 7 个 `nexcore_*` 导出（内部单元符号被 `-exported_symbols_list` 隐藏） |

以上均在仓库副本执行（`build/` 被 macOS 封存，见 `docs/toolchain.md`），`experiments/third_party/` 未删。

## 4. 给后来人的三句话

1. 「P1 全绿」= 上述五条中的机械部分全绿，**不含 JS 语义**；纸面退出只剩 Android NDK 与 Kotlin/JVM 两条**不可验证**项，登记在中国，没有冒充通过。
2. 想验证 JS 能力，跑 `bash experiments/build.sh`，不要看 `check.sh`。
3. 谁要动 JS 接线，先读 P2 卡草案的前置条件——`js_host` 必须重新注册，只改 `IMPL_UNITS` 会在 ④ 红。
