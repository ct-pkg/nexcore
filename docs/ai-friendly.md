# AI 友好性约束

“AI 友好”不是让 AI 随意改代码，也不是让框架生成业务内容；而是让 AI 在**明确的层、明确的输入、可机器检查的边界**内高速产出**宿主侧**代码。框架仓库交付的是约束（Schema 形态、生成器规则、可检查项），业务 JS、UI 与业务 Schema 内容属于宿主项目。

## 1. 分层权限矩阵

| 路径/产物 | 归属 | AI 可自由生成 | AI 可提议，需人工确认 | AI 禁止直接修改 |
|---|---|---|---|---|
| `js/app/**`（业务编排） | 宿主 | 是（只用生成的 `nexcore` API） | — | — |
| `js/tests/**`、`tests/**` | 宿主 | 是 | — | — |
| `core/**`（业务逻辑） | 宿主 | — | 是（不含线程/生命周期） | 直接依赖框架内部 |
| `schema/nexcore.schema.yaml` | 宿主 | — | 是（P3 起；必须附迁移说明） | 与已发布版本冲突的破坏性改动 |
| `src/units.json`（当前阶段的单元/符号清单） | 框架 | — | 是（新增单元/recipe 必须同步） | 与实际单元路径/命名空间/cname 不一致的手改 |
| `platform/**`（transport、JNI） | 宿主（可用框架模板） | — | 是 | 绕过 drain/生命周期契约 |
| `docs/**`（说明、示例） | 框架 | 是 | 涉及决策/验收判据的改动 | 已批准的决策记录 |
| `schema/generated/**` | 框架工具链产出，落宿主仓库 | 只能由生成器产出 | — | 手写任何一行 |
| `src/**`（ABI、线程、队列、QuickJS 运行时宿主） | 框架 | — | — | 是 |
| `tools/check.sh`（构建 + 机械检查唯一入口） | 框架（**编码侧唯一维护**） | — | — | 是（需求侧只提检查项需求，不编辑脚本） |
| `scripts/**`（检查、CI） | 框架 | — | 是 | 放宽检查规则的改动 |
| `docs/decisions.md`、`docs/acceptance.md` | 框架 | — | 是 | 未经评审删除条目 |

判定原则：

- **AI 可以写“被边界包住的东西”**：宿主业务 JS、宿主测试、宿主 UI、文档。
- **AI 不可以写“定义边界的东西”**：ABI、线程模型、关闭顺序、协议分发、错误码表、生成器模板。
- **生成物永远不是手写目标**：AI 可以运行生成器，但不能直接编辑 `schema/generated/`。
- **框架不产出业务内容**：AI 在宿主项目里生成业务代码，框架仓库只保证生成得出来、检查得到。

## 2. 文件所有权与 CODEOWNERS 建议

```
# .github/CODEOWNERS
/src/            @core-reviewer
/platform/          @platform-reviewer
/schema/            @schema-reviewer
/schema/generated/  @schema-reviewer
/core/              @domain-reviewer
/js/                @js-reviewer
/docs/decisions.md  @tech-lead
/docs/acceptance.md @qa-lead
```

- `src/`、`platform/` 的 PR 必须有对应 reviewer 批准，AI 不能自我批准。
- `schema/generated/` 只接受“生成器输出 + `--check` 通过”的 PR；reviewer 只看 diff，不接受手改。
- `js/` 的 PR 可以由 AI 生成、CI 通过后快速合并，但仍需一人批准。
- **仓库归属**：`src/`、`scripts/`、`docs/` 在框架仓库；`js/`、`core/`、`schema/`、`platform/` 在宿主项目仓库。CODEOWNERS 需按仓库分别落地，不能把两套 owner 混在一张表里。

## 3. AI 任务契约

以下示例发生在**宿主项目**：框架给出任务模板与检查项，不提供示例里的业务实现。

每个 AI 编码任务必须携带以下信息，缺一不可：

```text
允许修改：
  - js/app/shop/cart.js
  - js/tests/cart.test.js
只读：
  - schema/generated/js/nexcore.api.js
  - schema/nexcore.schema.yaml   # P3 起才有
  - src/units.json               # 当前阶段的单元/符号清单
禁止：
  - 修改 src/、platform/、schema/generated/
任务：
  实现“购物车满 3 件显示提示”，只能调用生成的 ui.toast 与 core.cart.list。
约束：
  - 不新增方法；不拼接方法名字符串；不使用 eval/new Function/Proxy。
  - 所有 Promise 必须处理 rejection；不吞错误。
  - 运行 `npm test -- cart` 与 `scripts/check-boundaries.sh`。
输出：
  - 代码 + 测试 + 一段“边界声明”：修改了哪些层、调用了哪些生成方法。
```

规则：

1. **不发明方法**：只允许调用生成 API 中存在的方法；需要新方法时，先走 Schema 变更流程。
2. **不读内部**：AI 不应被要求阅读 `src/` 细节来完成任务；只给 Schema + 生成 API + 业务需求。
3. **不猜类型**：类型来自生成物；AI 不得手写 JSON 键名或类型转换。
4. **必须跑检查**：任务说明中列出要运行的命令；AI 的完成报告必须包含实际输出，而不是“应该通过”。
5. **小步提交**：一次任务只动一层；跨层改动拆成多个 PR。

## 4. Schema 变更如何传导到三端

### 4.1 标准流程

```
1. 提议：AI/人 修改 schema/nexcore.schema.yaml（P3 起；当前阶段改 src/units.json）
2. 校验：python3 tools/nexcore_gen.py --spec src/units.json --src src --check
3. 生成：python3 tools/nexcore_gen.py --spec src/units.json --src src（P3 起追加 --schema/--out）
4. 实现：core handler / ui handler / js handler
5. 测试：协议测试 + 三端单测 + 一条端到端用例
6. CI：生成物无漂移、C 编译、Swift/Kotlin 编译、边界检查
7. 人工评审：schema/units diff、生成 diff、迁移说明
8. 合并：四端同提交
```

### 4.2 增量变更

- 新增方法/事件、新增可选字段、新增错误码。
- 不升 `protocol_version`；更新 schema hash。
- 旧客户端遇到新 schema hash 会在 `create` 时得到 `NX_E_SCHEMA_MISMATCH`，因此发布时必须三端同版本；如果要求旧客户端兼容，只做增量并让旧客户端忽略未知字段/方法（旧客户端不会主动调用新方法，所以可兼容；hash 校验需要允许“旧 schema 是新的子集”策略，见 Q-14）。
- 默认策略：**同版本发布，hash 必须一致**；跨版本兼容不在 v1 承诺。

### 4.3 破坏性变更

- 删除/重命名方法、改类型/方向/超时语义。
- 升 `protocol_version`；生成器需要 `--allow-breaking`；manifest 标记。
- 三端同版本；不提供运行时兼容层。

### 4.4 AI 在变更中的角色

- 可以：改 Schema、跑生成器、写 handler 骨架、写测试、写迁移说明。
- 必须人工确认：Schema 语义、错误码、超时/取消语义、生成器模板改动、任何 `src/` 改动。
- 禁止：手改生成物、跳过 `--check`、在业务代码里写方法名字符串绕过生成 API。

## 5. 机器可检查的边界

以下脚本是 CI 的硬门槛；规则改动需要人工评审。

**归属**：`tools/check.sh` 由**编码侧唯一维护**。需求侧只交付检查需求（检什么 / 读哪个产物 / 失败判据 / 负例），不直接编辑脚本；本节与 design.md §5.7 同源，冲突时以 design.md §5.7 为准。

### 检查需求清单（需求侧口径）

| 编号 | 检什么 | 读哪个产物 | 失败判据 | 负例怎么造 |
|---|---|---|---|---|
| ⓪ | 生成器漂移：`nexcore_gen.py --check` 重生成后逐字节比对 | `src/units.json`、`src/**/*_map.m.ct` | 退出码非 0 或任一 diff 非空 | 手改 `queue_map.m.ct` 一行 recipe → 必须非 0 |
| ①a | Pass 1 只编 `src/NexCore.h.ct` | `build/abi/NexCore.h` | 退出码非 0；头缺失/为空 | 头单元写语法错误 → 必须非 0 |
| ①b | 发布头洁净：struct tag 集合 ⊆ {`NexCoreConfig`}（剥注释/字符串） | `build/abi/NexCore.h` | 出现其他 tag（前置声明或定义） | 加 `pub struct Foo;` → 必须失败；把 `queue.ct` 加进 Pass 1 → `queue_Ring` 必须失败 |
| ①c | 前置声明 golden 逐行一致（含顺序） | `build/abi/NexCore.h`、`abi/expected-forward-decls.txt` | `diff -u` 非空 | 基线多一行 `struct QueueFoo;` → 必须失败 |
| ②a | Pass 2 只列实现 `.ct`，带 `--ns-root src -I src` | `build/impl/*.c` | 退出码非 0；实现 `.c` 缺失 | 去掉 `-I src` → 必须非 0 |
| ②b | 每个实现 `.c` 过严格 `cc`（含 `-Werror=implicit-function-declaration`），`-I build/abi` 首位 | `build/impl/*.c` | 任一 error/warning | 调用未声明的 `zzz(v)` → 必须失败；跨单元 `&f` → 必须失败 |
| ②c | 7 个 `nexcore_*` 是定义符号 `T`（非 `U`/`t`），兼容 Mach-O `_?` | `build/impl/*.o` 的 `nm -g --defined-only` | 缺失、`U`、或 `t` | 把 `nexcore_create` 改成非 `pub`/static → 必须失败 |
| ③ | 生成 C 剥注释/字符串后无 `::` | `build/impl/*.c` | 残留 `::` | fixture 注入 `ns::f()` → 必须失败 |
| ④ | recipe callee == 符号表 cname == 导出头原型；无重复 key/未登记 cname | `src/units.json`、`build/impl/*.c`、`build/impl/*.h` | 任一不一致或缺失 | units.json 改成不存在的 `push2` → 必须失败 |
| ⑤a | 实现 `.ct` 强制首条 self import；import 只允许自身 `.h.ct` 或 `<ns>::<ns>_map`；`*_map.m.ct` 不得出现单段 import | `src/**/*.ct`（绝对路径判定） | 实现单元缺 self import、import 在 namespace 后、import 实现单元、映射单元出现单段 import | 删 `js_host.ct` 的 `import js_host;` → 必须失败；`queue_map.m.ct` 加 `import queue;` → 必须失败 |
| ⑤b | `units.json` 的 `methods` 与 `source` `.h.ct` 的 `pub` 函数集合**完全相等**（支持跨行声明） | `src/units.json`、各 `.h.ct` | 任一方向差集非空 | `.h.ct` 加 `pub fn peek` 不入表 → 必须失败；表里加不存在的 `pop` → 必须失败 |
| ⑥ | stat key 运行时契约：哨兵预填 `*out` 后逐键调用 `nexcore_stat` | `tests/stat_keys.c`、`build/impl/*.c`、`build/abi/NexCore.h` | `10..14` 返回 `NX_E_INTERNAL` 且 `*out` 不变；`0`/`19`/`20`/`1024` 返回 `NX_E_INVALID_ARGUMENT`；`1..9`/`15..18` 返回 `NX_OK` 并写值 | 10..14 改成返回 `NX_OK`/写 0 → 必须失败；`19` 当普通键 → 必须失败 |
| smoke | 严格参数链接并运行 `tests/smoke.c` + `build/impl/*.c` | `tests/smoke.c`、`build/impl/*.c`、`build/abi/NexCore.h` | 链接失败或退出码非 0 | 删一条 map recipe → 链接 `U`，必须失败 |

**状态**：上表全部检查项（`0`、①a~①c、②a~②c、③、④、⑤a、⑤b、⑥、smoke）均已由编码侧**全量实跑通过**（真实 `SCRIPT_RC=0` + 逐项 PASS），每条都有真失败过的负例。⑤a/⑤b 此前的「待实跑确认」已撤销；⑥ 自加入起即为实跑通过。

### 5.1 `scripts/check-boundaries.sh`

```bash
# 1. Bridge 不得包含平台头
! grep -rnE '#include[[:space:]]*[<"](Foundation|UIKit|SwiftUI|jni\.h|android/)' src/

# 2. 只有 ABI 单元导出 nexcore_
test "$(grep -rl 'pub .*nexcore_' src/ | sort)" = "src/NexCore.h.ct"

# 3. .ct 中不得出现 #ifdef/#ifndef/#if
! grep -rnE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)' --include='*.ct' src/ core/

# 4. .ct 中不得使用 errno 标识符
! grep -rnE '\berrno\b' --include='*.ct' src/ core/

# 5. import 必须在 namespace 之前
#    （逐文件取行号比较；见 scripts/check-import-order.py）

# 6. 禁止跨单元取函数地址：&qualified / = qualified / (&f)
! grep -rnE '(&|=)[[:space:]]*[A-Za-z_][A-Za-z0-9_]*::' --include='*.ct' --include='*.c' src/ core/ schema/generated/c

# 7. 跨 ABI 结构体不得带 trait；生成器也会递归检查
#    grep 只能覆盖同一行的写法，跨行的 `use` 由生成器/评审兜底
! grep -rnE '^[[:space:]]*(pub[[:space:]]+)?struct[[:space:]].*use[[:space:]]+' --include='*.ct' src/ core/

# 8. JS 业务不得直接调用内部函数
! grep -rnE '__nx_(call|core_call|reply|emit|subscribe)' js/app/

# 9. 生成物不得被手改：与生成器输出逐字节比对
scripts/check-generated.sh

# 10. 单元拓扑（不要用一条 grep 代替；编号沿用检查清单 ⑤a/⑤b）
# ⑤a-1. 每个实现 .ct 必须以 import <self>; 开头（缺了 CT 静默 exit 0，但会丢命名空间前缀）
# ⑤a-2. 只允许 import 自身 .h.ct 与 <ns>::<ns>_map；*_map.m.ct 也要检查，不能整类跳过
#        （映射单元没有自身 .h.ct，所以任何单段 import 都违规）
# ⑤a 由 tools/check.sh 按解析后的绝对路径判定（脚本内 Python，不用 grep 近似）
# ⑤b. units.json ↔ .h.ct 方法表一致（双源拦截）：两者 pub 函数集合必须相等
#     由 tools/check.sh ⑤b 实现（解析 .h.ct 的 pub 函数名并比对 methods）；已实跑通过

# 11. 跨单元调用/取址：由编译器前端判定，不再用正则扫描 C
cc -std=c99 -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
   -I build/abi -I build/impl -I schema/generated/c -I src/js_host -I src/queue \
   -fsyntax-only build/impl/*.c schema/generated/c/*.c
```

### 5.2 `scripts/check-generated.sh`

- 在临时目录运行生成器；
- 与 `schema/generated/` 逐字节 `diff -r`；
- 失败时打印第一个差异与“请运行 nexcore-gen，不要手改”。

### 5.3 `scripts/check-schema.sh`

- 运行 `python3 tools/nexcore_gen.py --spec src/units.json --src src --check`；P3 起追加 `--schema` 校验；
- 检查每个 `core` 方法有 `c_handler`，每个 `ui` 方法在 Swift/Kotlin 生成物中有对应协议方法，每个 `js` 方法在 JS 生成物中有 handler 名；
- 检查命名冲突、保留字、重复名。

### 5.4 `scripts/check-forbidden.sh`

```bash
# 生成物与 JS 业务中的“魔法”
! grep -rnE '\beval[[:space:]]*\(|new[[:space:]]+Function|new[[:space:]]+Proxy|\bwith[[:space:]]*\(' schema/generated/js js/app
! grep -rn 'Mirror' schema/generated/swift
! grep -rn 'kotlin-reflect' schema/generated/kotlin
! grep -rnE 'dlsym|dlopen' src/ core/
```

### 5.5 CI 顺序

```
schema 校验 -> 生成物漂移 -> C 编译 (-Werror) -> Swift 编译 -> Kotlin 编译
-> 单元测试 -> 协议一致性 -> 线程/关闭压力 -> TSAN/ASAN -> 边界检查
```

任何一步失败都不允许合并；不允许 `--no-verify`。

## 6. 人工确认清单

### 6.1 任何 `src/` 改动

- [ ] 是否新增了 ABI 函数？若是，ABI 版本是否升？
- [ ] 是否改变了线程归属（JS/drain/UI）？
- [ ] 是否在锁内调用了 sink/JS/Core？
- [ ] 是否新增了阻塞等待？是否可能死锁？
- [ ] 是否改变了 shutdown/free 顺序？
- [ ] 是否让 `JSValue` 跨线程/跨 turn？
- [ ] 是否绕过了 schema 白名单？
- [ ] 是否新增了平台头/平台 API？
- [ ] 是否通过了 TSAN/ASAN 与 RC-01..RC-16 用例？

### 6.2 任何 `schema/` 改动

- [ ] 方法名/方向/超时/可取消语义是否明确？
- [ ] `c_handler` 是否存在且签名符合约定？
- [ ] 是否破坏兼容？是否升 `protocol_version`？
- [ ] 错误码是否复用现有语义，而不是新造同义码？
- [ ] 生成物是否四端齐全、无漂移？
- [ ] 跨单元调用是否只经映射单元：每个实现 `.ct` **必须**有 `import <self>;`，其余 import 只允许 `<ns>::<ns>_map`；`*_map.m.ct` 内部也按同一白名单检查；调用写成 `<ns>::<Trait>.<method>`，没有裸名或手写 cname（GEN-17/SYM-04）；**自身 `.h.ct` 是合法的声明可见性，不是跨单元依赖**？
- [ ] `units.json` 的 `methods` 是否与 `.h.ct` 的所有 `pub` 函数集合完全相等（check.sh ⑤b，已实跑通过）？新增 `pub` 函数要么列入，要么改成非 `pub`。
- [ ] 发布头是否来自 Pass 1（只编 `src/NexCore.h.ct`），且除 `NexCoreConfig` 外没有其他 struct 名（ACC-A-01）？
- [ ] `&name` 是否只指向本单元定义或本单元生成的 wrapper（GEN-16/SYM-03）？
- [ ] 生成物是否通过 `cc -Werror=implicit-function-declaration -fsyntax-only`（替代旧的符号扫描脚本）？
- [ ] 是否补充了协议测试与一条端到端用例？

### 6.3 任何 `core/` 改动

- [ ] 是否纯函数（无 I/O、无全局可变状态、无线程）？
- [ ] 是否只使用 `NxArena` 分配输出？
- [ ] 是否在错误路径释放了 arena？
- [ ] 是否返回了定义好的错误码？
- [ ] 是否避免了 `int64` 超安全整数？金额是否用最小单位？
- [ ] 是否没有调用 `nexcore_send`/JS/平台 API？

### 6.4 任何 `js/app/` 改动

- [ ] 只调用生成的 `nexcore` API？
- [ ] 没有 `eval`/`new Function`/`Proxy`/动态方法名？
- [ ] 所有 Promise 都有 rejection 处理？
- [ ] 没有把大对象长期挂在全局？
- [ ] 没有假设事件与响应的相对顺序？
- [ ] 没有依赖 `setTimeout`（v1 不提供）？

### 6.5 任何 `platform/` 改动

- [ ] sink 内复制了 JSON，没有保存指针？
- [ ] 切换到 UI 线程的方式正确（`@MainActor` / `Dispatchers.Main`）？
- [ ] 关闭顺序是“停 drain → shutdown → free”？
- [ ] pending 在 shutdown 时全部以 `E_SHUTDOWN` 结束？
- [ ] JNI 没有缓存 `JNIEnv*` 跨线程？
- [ ] 没有在 UI 线程阻塞等待 JS？

## 7. AI 常见失败模式

| 失败模式 | 例子 | 防线 |
|---|---|---|
| 幻觉方法 | 调用 `nexcore.cart.remove`，Schema 里没有 | 生成 API 只含真实方法；运行时 `E_NO_METHOD`；边界检查 |
| 字符串化参数 | 手写 `{"duration_ms": x}` 而不是用生成函数 | 生成函数是唯一入口；`grep` 业务代码中的 JSON 键 |
| 吞错误 | `try { ... } catch (e) {}` | 生成模板 + lint（禁止空 catch） |
| 重试风暴 | `while (true) { await call() }` | 禁止无限重试；测试超时；review |
| 阻塞主线程 | Swift 在主线程 `nexcore_drain(-1)` | transport 模板固定；review |
| 手改生成物 | 直接改 `NexCoreAPI.swift` | CI `--check` |
| 绕过 Schema | 在 JS 里直接 `__nx_call` | `check-boundaries.sh` |
| 隐藏全局 | `globalThis.cache = {}` 长期持有大对象 | review + 内存验收 |
| 误用线程 | 在 JS 里 `Atomics.wait` / 阻塞 | QuickJS 无该 API；白名单 |
| 猜类型 | 把 `int64` 当 `number` 无范围检查 | 生成类型 + `E_INVALID_PARAMS` |

## 8. 度量

| 指标 | 目标 | 采集 |
|---|---|---|
| 生成物漂移 PR 数 | 0 | CI |
| 边界检查失败次数/百 PR | < 2 | CI |
| AI PR 平均评审时长 | 逐季下降 | GitHub |
| 协议/线程测试覆盖率 | ≥ 80% 分支 | coverage |
| 因 AI 改动导致的线上/测试崩溃 | 0 | 事故记录 |
| Schema 变更到四端合并的步骤数 | ≤ 5 | 流程审计 |

度量不是 KPI，用来发现“边界是否真的可执行”；如果边界检查经常误报，先修检查规则，而不是放宽规则。

## 9. 给 AI 的最小提示模板

```text
只允许修改：<路径列表>
只读：<Schema 路径>、<生成 API 路径>
禁止：src/、platform/、schema/generated/、任何手写方法名/JSON 键

可调用的方法（来自生成 API）：
  - ui.toast(text: string, duration_ms?: int32): Promise<void>
  - core.cart.list(): Promise<CartLine[]>

要求：
  - 不新增方法；不猜字段；不吞 Promise rejection；不用 eval/Proxy/动态方法名。
  - 修改后运行：<测试命令>、scripts/check-boundaries.sh、scripts/check-generated.sh。
  - 输出一段“边界声明”：改了哪层、调用了哪些生成方法、有哪些未决问题。

如果需求需要新方法：停止实现，先给 Schema 变更提议。
```

这条模板的作用是把“AI 不能做什么”变成任务输入的一部分，而不是事后靠 review 抓。
