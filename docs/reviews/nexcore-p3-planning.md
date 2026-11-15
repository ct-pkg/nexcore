# P3 规划（只规划，不施工）

> 日期：2026-11-15 ｜ 依据：`docs/roadmap.md:116-164`（P3 范围与退出条件，不改动）
> 结论：P3 的**生成器侧可以在本机开工**；两条验收（Kotlin/JVM、Android NDK）**必须等环境**，不能在本机算通过。

## 1. 起点状态

| 能力 | 现状 |
| --- | --- |
| ABI 头 | `build/abi/NexCore.h` 可生成，7 个函数，`cc -Werror` 通过 |
| 动态库 | `build/bin/libnexcore.dylib` 可产出，`otool -L` 只有 libSystem，`nm -gU` 恰好 7 个 `nexcore_*` |
| JS 后端 | 两档：null（空实现）/ quickjs（`js_backend_quickjs.ct`），均 rc=0；quickjs 档 ⑧⑨/smoke 真跑 |
| 单元生成 | `units.json` → `*_map.m.ct`，`--check` 漂移可检 |
| 门禁 | `check.sh` 有 ⓪①②③④⑤⑥⑦⑩ 与 ⑤c、g0；`g1..g9` 仍是退出码 2 的占位 |
| 未闭合的验收 | ACC-A-04/06/07/08/09/10/11 |

## 2. 未闭合项，按「能不能在本机推进」分三类

### A. 本机可推进（不需要新环境）

| 项 | 缺什么 | 为什么可本机 |
| --- | --- | --- |
| ACC-A-04 ABI 版本/布局冻结 | 负例：改字段不升 `abi_version`、`struct_size` 尾部垃圾 | 只需在 `tests/` 加测例 + golden |
| ACC-A-08 多线程 drain | barrier + 重复 100 次 | 纯 pthread 测例 |
| ACC-A-09 时钟注入 | 假 `now_ms` 推进 24h | 需先给 drain 注入时钟钩子（小改） |
| ACC-A-10 布局 golden | 生成并入库 `abi/abi-layout.txt` 比对 | 生成器已有 emit-c，扩一段即可 |
| ACC-A-11 `schema_hash` 不匹配 | 负例 create + 重试 | 纯测例 |
| `g1..g9` 门禁 | 9 个子命令未实现 | 机械脚本工作 |
| P3 生成器 `nexcore-gen` | Schema→四类产物、GEN-01..20、20 条负例、golden、`--check` | Python + PyYAML，锁版本即可 |

### B. 需要环境（本机不具备，不许记通过）

| 项 | 缺的环境 | 影响 |
| --- | --- | --- |
| ACC-A-06 Kotlin/JVM | `kotlinc` / JDK + JNI 工具链 | P3 退出条件含 Kotlin 产物编译 |
| ACC-A-07 Android NDK | NDK + `gradlew` 工程 | P3 退出条件明确要求「Android 工具链编译」 |

### C. 需要外部授权或决策（不是技术活）

| 项 | 状态 | 备注 |
| --- | --- | --- |
| 主规范 L-7 收录 nexcore | 待授权 | 变更单已备：`docs/reviews/nexcore-l7-authorization.md`；不授权则 G0 永远判「未纳管」 |
| Q-15 日志/遥测/PII 边界 | OPEN | 时限 P4 发版前，但会影响 P3 生成器的日志埋点形态 |
| Q-16 性能参考设备与阈值 | OPEN | 时限 P4 基准前，P3 只需预留采样点 |

## 3. 建议的开工顺序（三步）

1. **先补 A 类里与 ABI 稳定性相关的测例**（ACC-A-04、A-10、A-11）。理由：P3 的生成器要产出 registry 与 `_Static_assert`，ABI 布局必须先被 golden 锁住，否则生成器的「布局不变」承诺没有基线可比。
2. **再动 P3 生成器骨架**：`tests/fixture.schema.yaml` + GEN 最小集（映射单元、回调 wrapper、符号表校验）+ `--check`。这一步不依赖 CT 新能力，也不依赖 Android/JVM。
3. **最后处理 B 类环境项**：等 NDK/Kotlin 到位后再关 A-06/A-07；在此之前，P3 的「Android 编译」与「Kotlin 编译」两项标记为**不可验证**，不进绿线。

## 4. 风险与遗留

| 风险 | 说明 | 建议 |
| --- | --- | --- |
| 生成器与 `units.json` 双源漂移 | P3 生成器读 Schema，P1/P2 读 `units.json` | 生成器输出 `units.json` 的派生视图，或让 `--check` 同时校验两者 |
| QuickJS 版本无摘要校验 | `experiments/build.sh` 只锁 `QJS_VERSION`，没有 sha256 | 在 P3 期间补 `QJS_SHA256` 校验（一次现场量取） |
| `src/js_host/*` 尚未删除 | 需跨 2 个独立提交的全绿证据（卡 §10） | 不阻塞 P3；满足条件后单独提交删除 |

## 5. 明确不做

- 不改 `docs/roadmap.md` 的 P3 范围与退出条件、不改验收条目、不动 `.ai-engineering`。
- 不建 P3 的 phase 卡、不写任何生成器代码（本文件只规划）。
- 不承诺 Kotlin/Native（Q-07 已定为不在 v1 验收范围）。
- 不在本机伪造 Android/JVM 通过。
