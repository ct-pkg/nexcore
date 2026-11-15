# PH-12 卡：挂账收口（K-1/K-3）+ shell 宿主阶段 2

> 状态：**已冻结（2026-11-15）**，开工。
> `version-minor`：**12**（收口时产品版本 MINOR 必须为 12）。
> 上游：PH-11 卡 §10（挂账 K-1/K-3）、[nexcore-shell-host-ruling.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-ruling.md) §6.2 阶段 2、[nexcore-ph11-stage1-battle.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-ph11-stage1-battle.md) §3.2。
> 禁止改动：`docs/roadmap.md`、`docs/acceptance.md`、`.ai-engineering`。

---

## 1. 归属

| 交付物 | 归属 | 依据 |
| --- | --- | --- |
| K-1 归档档位判据 | **门禁层**（`tools/check.sh` ⑰） | 只加静态断言，不改库实现 |
| K-3 ABI 错误码评审 | **契约层**（结论落文档，不改 ABI） | ABI 码是跨端契约，加码要同步 Swift/Kotlin |
| `-e` / `process.argv/env` / stdin | **宿主层**（`hosts/shell/main.c`） | 只消费公开 ABI；注入走 bootstrap 字符串 |
| SIGINT 标记 | **宿主层**（能覆盖的部分）+ **挂账**（中断求值需引擎钩子） | 同步求值期间无中断入口，需库层 `JS_SetInterruptHandler` |

---

## 2. 不变量

| # | 不变量 | 核对 |
| --- | --- | --- |
| I-1 | 仍是 7 个导出函数、单跨 ABI 结构体 | ⑩ ①b |
| I-2 | `abi_version` 不升、`abi-layout.txt` 不变 | ⑪ ⑫ |
| I-3 | 两档 rc=0，null 档 SKIP 仍为 3 | 两档 `ALL CHECKS PASSED` |
| I-4 | ⑮⑯⑰ 不回退 | 两档对应 PASS |
| I-5 | `nex` 不进 `check.sh` 绿线 | `grep -c check-host tools/check.sh` == 0 |
| I-6 | 不改 `acceptance.md` / `roadmap.md` | `git diff --name-only` |

---

## 3. 设计定死项

### 3.1 D1 K-1：档位错配必须可判定

- quickjs 档：归档必须含 `js_backend_quickjs.o`；且 `js_backend.o`（header stub）**不得定义任何 `T` 符号**。
- null 档：归档**不得**含 `js_backend_quickjs.o`。
- 断言对象是「从归档里取出的成员」，不是 `build/impl` 目录，避免目录与归档不一致时判据空转。

### 3.2 D2 K-3：评审结论 = 不新增 ABI 错误码

- 阶段 2 的宿主只需要「成功 / 失败」二值，错误分类用 `$error` 的 `code` 字符串承载即可。
- `NX_E_JS_EXCEPTION` 属跨端契约（Swift/Kotlin 要同步枚举与文档），当前收益不抵成本。
- **复活条件**：移动端提出「按错误类别走不同审计/重试策略」，或需要 `detail`（行号/栈）时，另开 ABI 变更评审；不得在宿主 phase 里顺手加。
- 判据：`abi/abi-layout.txt` 中 `NX_E_JS_EXCEPTION` 出现次数为 **0**。

### 3.3 D3 阶段 2 宿主能力（全部在宿主内，不改库）

- `nex -e '<code>'`：内联求值（程序模式），与文件路径二选一。
- `nex -`：从 stdin 读脚本。
- `process.argv` / `process.env`：宿主在 bootstrap 里注入只读快照。
  - `process.argv = ["nex", <script>, ...args]`（`-e` 时为 `["nex", "-e", <code>, ...args]`）。
  - `process.env`：从 `environ` 拷贝 `KEY=VALUE`，值为字符串。
- `SIGINT`：`signal(SIGINT, …)` 置标记；drain 返回后若已收到信号则 exit 130。同步求值进行中的中断**不做**（无引擎钩子），记挂账 K-4。

### 3.4 D4 宿主门禁扩展

- `tools/check-host.sh` 增加：`-e`、stdin、`argv/env`、SIGINT（可测部分）。
- 每条都是二值断言（输出字符串、rc、`process.argv[1]` 值）。

---

## 4. 任务清单

| task-id | deliverable | verify | commit | rollback | effort | 归属 |
| --- | --- | --- | --- | --- | --- | --- |
| PH12-T1 | ⑰ 增档位断言（stub 无 T / null 档无引擎对象） | 故障注入：把 null 档的 `js_backend.o` 塞进 quickjs 归档，断言必须非零并点名 | `test(check): PH12-T1 红` + `chore(check): PH12-T1 …` | 单提交 revert | S | 门禁 |
| PH12-T2 | K-3 评审结论落档 | `grep -c NX_E_JS_EXCEPTION abi/abi-layout.txt` == 0 | `docs(abi): PH12-T2 …`（`no-test:` 评审） | revert | S | 契约 |
| PH12-T3 | `-e` / stdin / argv / env | `bash tools/check-host.sh` → `HOST OK` 且新增 4 条断言 | `test(host): PH12-T3 红` + `feat(host): PH12-T3 …` | 成对 revert | M | 宿主 |
| PH12-T4 | SIGINT 标记（求值前/后） | 夹具：向自身 `kill -INT` 后 rc=130 | 同上 | 成对 revert | S | 宿主 |

---

## 5. 退出判据

| # | 判据 | 命令与阈值 | 对应 |
| --- | --- | --- | --- |
| E1 | 档位错配必红 | 故障注入脚本输出含 `档位错配` 且 rc≠0；正常归档下 ⑰ PASS | T1 |
| E2 | ABI 未扩 | `grep -c NX_E_JS_EXCEPTION abi/abi-layout.txt` → `0`；⑪ 通过 | T2 |
| E3 | `-e` 可用 | `nex -e 'var a=1; a+1'` → stdout `2`，rc=0 | T3 |
| E4 | stdin 可用 | `printf '1+2\n' | nex -` → `3`，rc=0 | T3 |
| E5 | argv/env 可见 | 脚本 `process.argv[1]` → 脚本路径；`process.env.NEX_T` → `v`（由夹具注入） | T3 |
| E6 | SIGINT 标记 | 夹具自 `kill -INT` → rc=130 | T4 |
| E7 | 全量门禁 | 两档 `ALL CHECKS PASSED`；null 档 SKIP=3；`HOST OK` | 全部 |

**UNVERIFIED 规则**：同步求值中的 SIGINT 中断无法验证，登记 K-4，不计通过。

---

## 6. 挂账（本 phase 新增）

| # | 事实 | 处置 |
| --- | --- | --- |
| K-4 | 求值期间无法响应 SIGINT：库未暴露 `JS_SetInterruptHandler`，宿主只能事后退出 | PH-13 候选（库层中断钩子 + 预算上限） |
| K-5 | `process.env` 是启动快照，脚本内修改不回写宿主环境 | 阶段 3 若需要再议；当前文档写清 |

---

## 7. 非目标

- 不做模块加载（`import`）、`require`、`-m`。
- 不做 `Shell.spawn` 与子进程（阶段 3）。
- 不做求值中的 SIGINT 中断（K-4）。
- 不新增 ABI 函数/错误码/结构体。
- 不改 `check.sh` 既有编号与既有判据。

---

## 8. 收口动作（如实登记）

| 项 | 事实 | 补跑命令 |
| --- | --- | --- |
| 仓库根 `build/` | 被 macOS 封存，实跑在 `/tmp/nx12` | `rsync -a --exclude build --exclude .git <repo>/ /tmp/nx12/` |
| 收口 tag / push | 本轮授权不做 | `git tag -a v0.12.0 -m "PH-12 <sha>" <sha> && git push origin v0.12.0` |

---

## 9. 收口证据表（2026-11-15，`/tmp/nx12` 副本实跑）

| # | 判据 | 命令 | 原始输出摘要 | rc | 结论 |
| --- | --- | --- | --- | --- | --- |
| E1 | 档位错配必红 | 故障注入：PATH 前置 `ar` 包装，把 null 档 `js_backend.o` 塞进 quickjs 归档后跑 `check.sh` | `⑰ FAIL: js_backend.o 是 header stub，不得定义 T 符号（档位错配）：` + `_js_backend_eval/_init/_pump/_shutdown/_stats` | 1 | PASS |
| E1b | 正常归档不误红 | 两档 `bash tools/check.sh` | `⑰ archive: PASS（对象集合 / 档位 / …）`；null 档 `N/A-RUN` | 0 | PASS |
| E2 | ABI 未扩 | `grep -c NX_E_JS_EXCEPTION abi/abi-layout.txt` | `0` | 0 | PASS |
| E3 | `-e` 可用 | `nex -e 'var a=1; a+1'` | `2` | 0 | PASS |
| E4 | stdin 可用 | `printf '1+2\n' \| nex -` | `3` | 0 | PASS |
| E5 | argv/env 可见 | 夹具脚本 `Shell.print(process.argv[1]+"\|"+process.env.NEX_T)` | `<tmp>/argv.nexjs\|v` | 0 | PASS |
| E6 | SIGINT 标记 | 后台跑自旋脚本 300ms，100ms 时 `kill -INT` | 退出码 `130` | 130 | PASS |
| E7 | 全量门禁 | 两档 `check.sh` + `check-host.sh` | `ALL CHECKS PASSED (quickjs)` / `(null)`，SKIP=3；`HOST OK` | 0 | PASS |

**红证记录**

| task | baseline | 命令 | 原始输出（节选） | rc |
| --- | --- | --- | --- | --- |
| PH12-T1 | `19053db` | 故障注入后的 `check.sh` | `⑰ FAIL: js_backend.o 是 header stub，不得定义 T 符号（档位错配）` | 1 |
| PH12-T3/T4 | `f5d10e2` | `bash tools/check-host.sh`（实现前） | `nex: cannot read -e` / `check-host: FAIL -e 退出码非 0` | 1 |

**说明**：T1 的红证是外部故障注入（`PATH` 前置 `ar` 包装），不靠改 `check.sh` 制造失败；注入脚本与证据在 `/tmp`，未入库。

---

## 10. 遗留项

| # | 项 | 状态 |
| --- | --- | --- |
| K-4 | 求值期间无法响应 SIGINT（无引擎中断钩子） | 挂账，PH-13 候选（库层 `JS_SetInterruptHandler` + 预算） |
| K-5 | `process.env` 是启动快照，脚本内修改不回写 | 已在 `hosts/README.md` 写清；阶段 3 再议 |
| K-3 | ABI 错误码评审 | **关闭**：结论不新增（[评审](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-ph12-k3-abi-error-code-review.md)） |
| K-1 | 归档档位判据 | **关闭**：⑰ 已增断言，红证见 §9 |
