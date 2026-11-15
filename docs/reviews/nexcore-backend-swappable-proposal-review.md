# NexCore「后端可置换方案」提案评审

| 项 | 内容 |
| --- | --- |
| 评审对象 | 「后端可置换方案」提案（主张 P1 trait-vtable 不采纳、推荐 S1 编译期多后端、配套 S0 档位签名对拍、公开 ABI 禁 vptr） |
| 仓库基线 | `main` @ `a1260c2`（2026-11-15 09:45 +0800）；提案自称基线 `808fc53` |
| 工具链 | macOS 27.2 arm64；`ct v0.44.2`（`PATH="$HOME/.local/ct/0.44.2:$PATH"`）；Apple clang 21 |
| 纪律 | 只读仓库；全部实测在 `/tmp`；未改 `src/`；未 push、未打 tag；只新增本文并本地提交 |
| 提案原文 | **仓库内未找到提案文件**（`git ls-files` 无命中，工作树也无未跟踪副本）。以下按任务消息中转述的提案主张逐条核对；若原文另有条目，以原文补评 |

---

## 0. 结论表

| # | 提案主张 | 判定 | 关键证据 |
| --- | --- | --- | --- |
| 1 | CT 缺陷是「`impl` 固有方法跨单元调用报错」 | **不成立** | 跨单元静态/实例均能过；真正条件是「单元带 `namespace` + 静态固有方法」。见 §1 |
| 2 | 因此「后端内部用 trait 必须加 shim 间接调用」 | **不成立（范围被放大）** | 跨单元 trait impl + 自由函数工厂可用；实例方法跨单元可用。见 §1.3 |
| 3 | 提案按 `main @ 808fc53` 描述「现状」 | **已过期，且基线自相矛盾** | 该提交上 `src/js_backend/` 不存在、无档位切换；HEAD 已前进 33 个提交。见 §2 |
| 4 | `turns()` 今天刚被删 | **不成立** | HEAD 仍有 11 处 `turns`；被删的是 `js_host_map.m.ct`，不是 `turns` 符号。见 §2.3 |
| 5 | 判据 2：`otool -L libnexcore.a` 不得出现 QuickJS | **不成立（对 `.a` 空转）** | `.a` 只列成员，不列依赖；quickjs 档同样不出现 QuickJS。见 §3 |
| 6 | `objdump -p` 对 ELF 能看动态依赖 | **成立（仅动态可执行文件）** | 自造 ELF64 实测输出 `NEEDED libc.so.6`；不适用于 `.a`。见 §3.3 |
| 7 | §4.2 两档 `NexCore.c` 同 sha 只是推演 | **命题成立，但已可实测为真；判据偏弱** | 两档实算同为 `b7b35f47…`；S1 原型同为 `b4630fd7…`。见 §4 |
| 8 | P1 trait-vtable 不采纳 | **成立（当前需求下），但不是因为公开 ABI** | 运行期二选一必须同时链接两实现，与 ⑩ 冲突；内部 vtable 本身不改公开 ABI。见 §5 |
| 9 | 推荐 S1 编译期多后端（`src/backends/{null,quickjs}/`） | **有条件成立；「切换 import 搜索路径」描述不准** | 可跑通，但受 4 条 CT 约束；当前同目录切换已达成同样效果。见 §6 |
| 10 | 配套 S0「档位签名对拍」门禁 | **必要；但提案的测法与编号都不成立** | `⑤b` 只查 `units.json` 的接口头；原样 diff 原型行会因 typedef/内联函数指针不同而误报；⑮ 已被微任务泵占用。见 §7 |
| 11 | 公开 ABI 禁 vptr | **成立，且已是冻结决策** | D-12 与 `NexCore.h.ct:92-93`；不是提案新增约束。见 §5.2 |
| 12 | CI 按档位条件化测试 | **风险成立：null 档会掩盖 quickjs 失败** | 打断 quickjs 的 `pump` 后，null 档 `rc=0`，quickjs 档 `⑮ FAIL`。见 §8 |

一句话：**S1 可以做，但提案对 CT 缺陷的归因、对基线的引用、对归档判据的写法、对 S0 的测法都需要改；当前同目录双实现已经在工作，S1 不是解决签名漂移的必要条件，S0 才是。**

---

## 1. 必核项 1：CT 缺陷描述与「必须加 shim」约束

### 1.1 提案描述与缺陷单不一致

提案写「`impl` 固有方法跨单元调用报错」。缺陷单的结论是：

> 在**带 `namespace` 的单元**里，`impl Ring { static int open(int n) … }` 已正确落码
> `ring_Ring_open` 并写进生成头，但调用点写 `Ring.open(4)` 会报
> `use of an undeclared variable`。**同一个单元里也报**；把 `namespace` 去掉，同单元和跨单元都能编过。
> 所以这不是「跨单元」问题，而是「命名空间 + 固有静态方法调用」问题。
>
> —— [ct-defect-namespaced-inherent-static.md:17-23](file:///Users/samxx/trae-ws/nexcore/docs/reviews/ct-defect-namespaced-inherent-static.md#L17-L23)

边界矩阵（同文件 [§3.4](file:///Users/samxx/trae-ws/nexcore/docs/reviews/ct-defect-namespaced-inherent-static.md#L141-L148)）：

| 单元有 `namespace` | 调用形态 | 同单元 | 跨单元 |
| --- | --- | --- | --- |
| 有 | `Type.method(...)`（静态） | **FAIL** | **FAIL** |
| 有 | `r.method(...)`（实例） | PASS | PASS |
| 无 | `Type.method(...)`（静态） | PASS | PASS |

### 1.2 本机复跑（`ct v0.44.2`）

在 `/tmp/nxdef` 复制复现工程，绝对路径调用（相对路径在 `/tmp` 副本下会报 `ctc: cannot open the input file`）：

```text
$ ct build --emit-c --ns-root /tmp/nxdef -I /tmp/nxdef /tmp/nxdef/use/use.ct -o /tmp/nxdef/o1
/tmp/nxdef/use/use.ct:6:16: error: use of an undeclared variable
rc=1

$ ct build --emit-c --ns-root /tmp/nxdef /tmp/nxdef/ring/same_unit.ct -o /tmp/nxdef/o2
/tmp/nxdef/ring/same_unit.ct:20:16: error: use of an undeclared variable
rc=1

$ ct build --emit-c -I /tmp/nxdef/flat /tmp/nxdef/flat_use/flat_use.ct -o /tmp/nxdef/o4
cc -std=c99 -I /tmp/nxdef/o4 /tmp/nxdef/o4/flat_use.c /tmp/nxdef/o4/Ring.c -o a.out
rc=0

$ ct build --emit-c --ns-root /tmp/nxdef -I /tmp/nxdef /tmp/nxdef/inst_use/inst_use.ct -o /tmp/nxdef/o6
cc -std=c99 -I /tmp/nxdef/o6 /tmp/nxdef/o6/inst_use.c /tmp/nxdef/o6/inst.c -o a.out
rc=0
```

### 1.3 由此推出的约束错在哪

提案的推论是「后端内部用 trait 必须加 shim 间接调用」。这条约束**不成立**：

1. **跨单元 trait impl 可用**。`experiments/oo_shim/p4_vtable_cross_unit` 里实现单元 `import jbif::jbif;`，用自由函数 `jbif_backend_create()` 做工厂，跨单元实现 trait；实测：

   ```text
   $ ./bin
   turns=0 eval=-1 magic=7
   ```

   生成 C 是 `h->js.vt->turns(h->js.self)`（`/tmp/nxp4/out/app.c:15`）。这里没有额外 shim 层，trait 分派本身就在用。

2. **命名空间实例方法跨单元可用**。上面的 `o6` rc=0；消费者只需持有带类型的接收者。
3. **无 namespace 的静态固有方法跨单元可用**。上面的 `o4` rc=0。
4. 真正失败的只有「**命名空间单元 + 静态固有方法**」，而且它失败在调用点名字解析，不是链接或可见性（缺陷单 §2.1）。

因此正确的表述是：

- 不是「`impl` 固有方法跨单元不能用」；
- 而是「**带 `namespace` 的单元不能用 `Type.static_method(...)` 做构造入口**」；
- 替代方案是**自由函数工厂**或实例方法，不需要再加一层 shim；
- 对 S1 而言，后端单元明确「无 namespace」，连这条限制都不触发；用 `js_backend_null` / `js_backend_quickjs` 直接导出 C 符号即可。

**判定：提案的缺陷描述不成立；由它推出的「后端内部用 trait 必须加 shim」不成立。**

---

## 2. 必核项 2：基准 `808fc53` 与当前 HEAD 的逐条对照

### 2.1 距离与文件面

```text
$ git rev-list --count 808fc53..HEAD
33

$ git ls-tree -r --name-only 808fc53 src/ | grep -c js_backend
0
$ git ls-tree -r --name-only HEAD src/ | grep -c js_backend
5
$ git ls-tree -r --name-only 808fc53 src/ | grep -c js_host
3
$ git ls-tree -r --name-only HEAD src/ | grep -c js_host
2
```

`808fc53` 上只有 `src/js_host/{js_host.ct,js_host.h.ct,js_host_map.m.ct}`；HEAD 上 `src/js_backend/` 是五个文件，`js_host_map.m.ct` 已删（`834736a`）。

### 2.2 基线本身就没有提案说的「两档」

```text
$ git show 808fc53:src/units.json
… "namespace": "js_host", "trait": "Host", "methods": ["init","eval","shutdown","turns"] …

$ git show 808fc53:tools/check.sh | grep -n IMPL_UNITS
197:IMPL_UNITS=(NexCore queue/queue js_host/js_host protocol/protocol)
```

该提交上**没有** `NEXCORE_JS` 档位变量，也没有 `JS_BACKEND`；`js_host` 永远被编译，QuickJS 永远被链接。当前 HEAD 才在 [check.sh:41-61](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L41-L61) 引入 `null/quickjs` 二档。

所以提案一边写「基线 `808fc53`」，一边描述「已有两档实现/档位切换」，**两者不可能同时为真**；它引用的其实是更晚的状态。

### 2.3 逐条现状对照

| 提案涉及的「现状」 | `808fc53` | HEAD `a1260c2` | 判定 |
| --- | --- | --- | --- |
| JS 后端是 `js_host` | 是（`units.json` 的 `Host`） | 否；`js_backend` + `Backend`（[units.json:10-13](file:///Users/samxx/trae-ws/nexcore/src/units.json#L10-L13)） | 已不成立 |
| `js_host_map.m.ct` 存在 | 是 | 已删（`834736a`） | 已不成立 |
| `js_host` 未废弃 | 是 | 已标 DEPRECATED（[js_host.h.ct:4](file:///Users/samxx/trae-ws/nexcore/src/js_host/js_host.h.ct#L4)、[js_host.ct:7](file:///Users/samxx/trae-ws/nexcore/src/js_host/js_host.ct#L7)） | 已不成立 |
| `turns()` 已删 | 未删 | **仍未删**：`units.json:13`、[js_backend.h.ct:35](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend.h.ct#L35)、[js_backend_map.m.ct:12/20](file:///Users/samxx/trae-ws/nexcore/src/js_backend/js_backend_map.m.ct#L12)、`js_backend.ct:60`、`js_backend_quickjs.ct:250`、`js_backend_quickjs.h.ct:36`、`js_host.ct:138`、`js_host.h.ct:27` | **不成立** |
| 没有 `libnexcore.a` | 是 | 否；`ar rcs` 在 [check.sh:457-465](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L457-L465)，⑰ 在 [check.sh:656](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L656) | 已不成立 |
| 没有 `hosts/` | 是 | 否；`hosts/README.md` 已落位，`hosts/shell/` 为空（[README:9](file:///Users/samxx/trae-ws/nexcore/hosts/README.md#L9)） | 已不成立 |
| 门禁编号到 ⑭ | 是 | 否；⑮ 微任务泵 [check.sh:630](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L630)、⑯ Provider [check.sh:641](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L641)、⑰ 归档 [check.sh:656](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L656) | 已不成立；S0 应使用 ⑱ |
| 后端只有 QuickJS 一档 | 是 | 否；null/quickjs 二档（[check.sh:41-61](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L41-L61)） | 已不成立 |

统计 `turns` 出现次数：

```text
$ grep -rn "turns" src/ | wc -l
11
```

**判定：提案的基线引用已过期；且「基线 808fc53 上已有两档」自相矛盾。`turns()` 被删的说法不成立。**

---

## 3. 必核项 3：`otool -L libnexcore.a` 不能作为判据

### 3.1 对静态归档的实测

在 `/tmp/nxeval` 分别跑两档全量检查，取 `build/bin/libnexcore.a`：

```text
$ otool -L /tmp/nxart/null/libnexcore.a
Archive : /tmp/nxart/null/libnexcore.a
/tmp/nxart/null/libnexcore.a(js_backend.o):
/tmp/nxart/null/libnexcore.a(NexCore.o):
/tmp/nxart/null/libnexcore.a(protocol.o):
/tmp/nxart/null/libnexcore.a(queue.o):

$ otool -L /tmp/nxart/quickjs/libnexcore.a
Archive : /tmp/nxart/quickjs/libnexcore.a
/tmp/nxart/quickjs/libnexcore.a(js_backend_quickjs.o):
/tmp/nxart/quickjs/libnexcore.a(js_backend.o):
/tmp/nxart/quickjs/libnexcore.a(NexCore.o):
/tmp/nxart/quickjs/libnexcore.a(protocol.o):
/tmp/nxart/quickjs/libnexcore.a(queue.o):
```

两档都 **rc=0**，两档都**没有任何依赖行**。「不得出现 QuickJS」在 quickjs 档也自动成立——这不是通过，是**空转**。静态归档里的对象没有最终链接的 load command，`otool -L` 只能列成员。

### 3.2 对 dylib 也同样不能证明「链接了 QuickJS」

```text
$ otool -L /tmp/nxart/quickjs/libnexcore.dylib
/usr/lib/libSystem.B.dylib
```

QuickJS 是**静态**链进 dylib 的，不会出现在 `otool -L` 里。该命令只能证明「没有动态依赖 QuickJS」，不能证明「代码里没有 QuickJS」。

### 3.3 `objdump -p` 对 ELF

自造一个 ELF64 动态可执行文件（`/tmp/mkelf.py`，带 `DT_NEEDED`）：

```text
$ objdump -p /tmp/dyn.elf
Dynamic Section:
  NEEDED libc.so.6
  STRTAB 0x0000000000400300
  STRSZ  0x000000000000000a
```

所以 `objdump -p` 对 **ELF 动态可执行文件**确实能看 `NEEDED`；但它对 `.a` 同样只打印成员/object 头，不打印依赖，且本仓库当前只产 Mach-O（`check.sh` 用 `cc` 默认目标），没有 ELF 构建路径。把它写成跨平台归档判据仍然不成立。

### 3.4 可用的替代判据

对**静态归档**，判「null 档不含 QuickJS」应查符号，而不是查 load command：

```text
$ nm -u /tmp/nxart/null/libnexcore.a | grep -cE 'JS_|js_std|quickjs'
0

$ nm -u /tmp/nxart/quickjs/libnexcore.a | grep -cE 'JS_|js_std|quickjs'
20
```

quickjs 档的未定义符号包括 `_JS_Eval`、`_JS_NewRuntime`、`_JS_ExecutePendingJob` 等；null 档为 0。这个差异是真实的、可红的。

建议的判据组合：

1. **归档对象集合**：`ar t` + `ar d` 负例（PH-10 E5 已有，[card.md:158](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L158)）。
2. **归档符号扫描**：null 档 `nm -u libnexcore.a | grep -E 'JS_|js_std|quickjs'` 必须为空；quickjs 档允许非空，并另查 `nm -g --defined-only` 的 `js_backend_` 符号集合。
3. **最终链接产物**：用**归档显式路径**链接探针（不要 `-lnexcore`，macOS 会优先选 `.dylib`；见 [stage0-redteam.md:285-290](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-shell-host-stage0-redteam.md#L285-L290)），再对**可执行文件**跑 `otool -L`；ELF 用 `objdump -p`/`readelf -d`。但这只能证明「没有动态依赖」，静态引擎仍需第 2 条符号扫描。

**判定：提案判据 2 不成立；`objdump -p` 对 ELF 成立但只适用于最终可执行文件，不能替代归档符号检查。**

---

## 4. 必核项 4：两档 `NexCore.c` 是否同 sha

### 4.1 当前 HEAD 的真实构建

在 `/tmp/nxeval` 用 `NEXCORE_JS=null` 与 `NEXCORE_JS=quickjs` 各跑一次全量检查（rc 均为 0），比较 `build/impl/NexCore.c`：

```text
b7b35f477ff08742248e6e96594670a62912a864536815bb47405b242cde9283  /tmp/nxart/null/NexCore.c
b7b35f477ff08742248e6e96594670a62912a864536815bb47405b242cde9283  /tmp/nxart/quickjs/NexCore.c

$ diff -u /tmp/nxart/null/NexCore.c /tmp/nxart/quickjs/NexCore.c
（无输出，rc=0）
```

### 4.2 在 S1 原型布局上再验一次

`/tmp/nxs1` 按 `src/backends/{null,quickjs}/` 布局（无 namespace，两个实现分别导出同一组 `js_backend_*` C 符号），两档分别生成消费者 C：

```text
b4630fd78e7601911d05c4bbebc7818d1a54d52860b941565f8637ec026051f6  outbn/NexCore.c
b4630fd78e7601911d05c4bbebc7818d1a54d52860b941565f8637ec026051f6  outbq/NexCore.c
$ diff -u outbn/NexCore.c outbq/NexCore.c
（无输出，rc=0）
```

行为确实不同：null 档 `eval=-1`，quickjs 档 `eval=42`；但消费者 `NexCore.c` 一字不差。

### 4.3 判定

- 提案 §4.2 自述是推演（[nexcore-oo-plan.md:324-325](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-oo-plan.md#L324-L325)）；**本次实测为真**，命题没有错。
- 但这条判据**偏弱**：它只证明消费者与接口不随档位变化，不证明两档实现等价。两档的 `libnexcore.a` 哈希不同（null `77a4751b…`，quickjs `486d0d8a…`），dylib 也不同；backend 的实现 C 更不同。
- 因此它可以作为「消费者零改动」的证据，但**不能替代** S0 签名对拍和 quickjs 档行为测试。

**判定：命题成立（已由推演变实测），但不足以单独支撑「后端可置换正确」。**

---

## 5. 其余主张

### 5.1 P1 trait-vtable 不采纳

**成立，但理由要写对。**

- 运行期 vtable 要求同一二进制里同时存在两套实现。`experiments/oo_shim/p3_vtable_runtime` 实测可运行期切换：

  ```text
  $ ./app          → turns=0 eval=-1
  $ ./app quick    → turns=1 eval=42
  ```

- 但 null 档若同时链接 QuickJS 就违反 ⑩（[check.sh:459-466](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L459-L466)）与 D-21；当前需求是**链接期二选一**，vtable 只增加一次间接跳转，不产生多态收益。
- 说「公开 ABI 禁 vptr 所以不能 vtable」**不准确**：vtable 可以只放在内部 `NexCoreHandleImpl.js`（[NexCore.ct:49-59](file:///Users/samxx/trae-ws/nexcore/src/NexCore.ct#L49-L59)），不跨公开 ABI。拒绝 P1 的真实理由是 `⑩` + 无运行期选择需求，不是 ABI。

**判定：P1 不采纳成立；若未来需要「一个二进制运行期 fallback / 插件注入」，P1 重新成为必要条件，不能把「不采纳」写成永久结论。**

### 5.2 公开 ABI 禁 vptr

**成立，且已是冻结决策，不是提案新增约束。**

- [NexCore.h.ct:92-93](file:///Users/samxx/trae-ws/nexcore/src/NexCore.h.ct#L92-L93)：

  > 唯一的跨 ABI 结构体。硬约束：
  > - 不使用 trait（trait 会追加隐藏 vptr，平台侧布局会静默错位）；

- [decisions.md:234-252](file:///Users/samxx/trae-ws/nexcore/docs/decisions.md#L234-L252) 的 D-12 已裁定「业务数据一律 JSON，跨 ABI 结构体禁止 trait」。
- 这条**不限制内部 trait**：`js_backend::Backend` 只在库内部，不跨 ABI；禁 vptr 只作用于 `NexCoreConfig` 等跨 ABI 结构。

**判定：成立；提案应写成「沿用 D-12」，不要写成本次新增设计。**

---

## 6. S1 `src/backends/{null,quickjs}/` 的真实约束

在 `/tmp/nxs1` 做了最小工程：接口与映射留在 `src/js_backend/`，两个实现放 `src/backends/null/js_backend_null.{h,ct}` 与 `src/backends/quickjs/js_backend_quickjs.{h,ct}`，无 namespace，导出同一组 `js_backend_*` C 符号。实测结论如下。

### 6.1 可以跑通

- 两档分别构建 rc=0；消费者 `NexCore.c` 同 sha（见 §4.2）；行为不同（null `eval=-1`，quickjs `eval=42`）。
- 关键不是「切换 import 搜索路径」，而是：
  1. `ct build` 传入哪一份实现 `.ct`；
  2. 编译该实现时，`-I` 指向哪一份**生成头**目录。
- 消费者只 `import js_backend::js_backend_map;`，映射 recipe 固定指向 `js_backend_*`，不需要知道后端目录。

### 6.2 约束 1：namespace 必须等于相对 `--ns-root` 的目录路径

实测：

```text
$ ct build --emit-c --ns-root /tmp/nxs1/src /tmp/nxs1/src/backends/null/ns_null.ct -o /tmp/nxs1/o_ns1
ctc: namespace 'null' does not match the directory 'backends/null'
rc=1

$ ct build --emit-c --ns-root /tmp/nxs1/src/backends /tmp/nxs1/src/backends/null/ns_null.ct -o /tmp/nxs1/o_ns2
cc -std=c99 -I /tmp/nxs1/o_ns2 /tmp/nxs1/o_ns2/ns_null.c -o a.out
rc=0
```

结论：`src/backends/null/` 下若写 `namespace null;`，必须把 `--ns-root` 设为 `src/backends`；在 `--ns-root src` 下不能写 `namespace null`。S1 选择**完全无 namespace** 是可行的，也是避免 `backends/null` 这种不合法 namespace 的正确做法。

### 6.3 约束 2：同名单元不能同时挂在两个 import 根

```text
$ ct build --emit-c -I /tmp/nxamb/a -I /tmp/nxamb/b /tmp/nxamb/use.ct -o /tmp/nxamb/out
/tmp/nxamb/use.ct:1:1: error: unit 'thing' exists in two import roots:
  '/tmp/nxamb/a/thing.h.ct' and '/tmp/nxamb/b/thing.h.ct'
rc=1
```

只挂一个根 rc=0。所以：

- 如果两个后端目录里的**单元名相同**（都叫 `js_backend`），`-I null -I quickjs` 会硬错；每次只能挂一个。
- 如果文件名区分（`js_backend_null` / `js_backend_quickjs`），两个根可以同时在 `-I` 上而不判歧义；但仍只应编译其中一个实现。

### 6.4 约束 3：输出文件名按 basename，同 basename 会静默覆盖

```text
$ ct build --emit-c --ns-root /tmp/nxcollide/src \
    /tmp/nxcollide/src/a/thing.ct /tmp/nxcollide/src/b/thing.ct -o /tmp/nxcollide/out
cc -std=c99 -I /tmp/nxcollide/out /tmp/nxcollide/out/thing.c /tmp/nxcollide/out/thing.c -o a.out
rc=0

$ ls /tmp/nxcollide/out
thing.c
$ head -1 /tmp/nxcollide/out/thing.c
/* Generated by ct v0.44.2 from /tmp/nxcollide/src/b/thing.ct … */
```

两个单元都叫 `thing`，只产出一个 `thing.c`，后一个覆盖前一个，打印的 `cc` 行还把同一路径列了两次。**这是 S1 最需要在卡片里写死的约束**：

- `src/backends/null/js_backend.ct` 与 `src/backends/quickjs/js_backend.ct` 如果同名，不能在同一次 `ct build` 里一起编译，也不能写进同一个 `-o` 目录；
- S0 签名对拍必须为每档准备独立的输出目录；
- 当前 `check.sh` 的 `build/impl` 是单目录，直接搬过去会踩这个坑。

### 6.5 约束 4：`⑤a` 强制 self-import，后端不能 import 接口单元

`⑤a` 扫描 `src/**/*.ct`（排除 `.h.ct`），要求实现单元第一条 `import` 是自身 stem，且只允许自身 `.h.ct` / `<ns>::<ns>_map`（[check.sh:311-316](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L311-L316)、[:327-350](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L327-L350)）。

因此：

- `src/backends/null/js_backend_null.ct` 必须第一条写 `import js_backend_null;`，且同目录必须有 `js_backend_null.h.ct`；
- 它**不能** `import js_backend;` 去复用接口头里的 `NxNativeCallFn`；
- 若 `init` 需要那个回调 typedef，只能在各自后端头里重复声明（当前 `js_backend_quickjs.h.ct:22-26` 就是这么做的）。

### 6.6 约束 5：`②a` / `⑤b` 的接线要改

- `②a` 的 `IMPL_UNITS` 当前是 `(NexCore queue/queue "$BACKEND_UNIT" protocol/protocol)`（[check.sh:229](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L229)）；`BACKEND_UNIT` 要指向 `backends/null/js_backend_null` 或 `backends/quickjs/js_backend_quickjs`。
- `②a` 先单独构建接口头（[check.sh:230-232](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L230-L232)）；S1 若仍把接口头生成到 `IMPL`，quickjs 档的 `build/impl/js_backend.c` 仍是接口头 stub，归档会出现一个空的 `js_backend.o`。干净副本实测：

  ```text
  $ ar t build/bin/libnexcore.a
  __.SYMDEF SORTED
  js_backend_quickjs.o
  js_backend.o
  NexCore.o
  protocol.o
  queue.o

  $ nm -g --defined-only build/impl/obj/js_backend.o
  （空）
  ```

  这与 [PH-10 card.md:116](file:///Users/samxx/trae-ws/nexcore/docs/phases/PH-10/card.md#L116) 的「quickjs 档没有 `js_backend.o`」矛盾。S1 应把接口头生成到独立目录，或让 ⑰ 排除 header stub 对象；否则对象集合判据会把空 stub 当成实现对象。
- `⑤b` 只读 `units.json` 的 `source`（[check.sh:364-398](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L364-L398)），不会看 `backends/*`；S0 必须另加。

### 6.7 判定

**S1 目录布局「可以做到」，但不是「换个目录就完事」；它至少带 5 条约束。更关键的是：当前同目录切换已经能达成同样效果（`NEXCORE_JS` 二选一，[check.sh:41-61](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L41-L61)），S1 不是修复签名漂移的必要条件，S0 才是。**

---

## 7. S0「档位签名对拍」：必要，但提案的测法不成立

### 7.1 必要性：`⑤b` 只查一份头

`⑤b` 遍历 `units.json` 的 `units`，只读 `unit["source"]`（[check.sh:376-377](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L376-L377)）。`js_backend_quickjs.h.ct` 不在 `units.json` 里，因此它的签名漂移**完全不被检查**。

实测负例：在 `/tmp/nxeval` 把 `js_backend_quickjs.h.ct` 的 `eval` 返回类型改成 `int64_t`，只跑 null 档：

```text
$ grep -n "pub int64_t eval" src/js_backend/js_backend_quickjs.h.ct
32:pub int64_t eval(void *h, const char *src, int32_t len, char **out, int32_t *out_len);

$ NEXCORE_JS=null bash tools/check.sh
…
⑤b units.json parity: PASS (2 units)
…
ALL CHECKS PASSED (NEXCORE_JS=null)
rc=0
```

签名已经漂移，门禁仍绿。**S0 的必要性成立。**

### 7.2 但「原型行逐行相等」会误报

生成两档头（`/tmp/nxsig`）：

```text
$ grep js_backend_init /tmp/nxsig/iface/js_backend.h
void * js_backend_init(const char *, int32_t, NxNativeCallFn, void *, const char *, int32_t);

$ grep js_backend_init /tmp/nxsig/qj/js_backend_quickjs.h
void * js_backend_init(const char *, int32_t, int32_t (*)(void *, const char *, size_t, const char *, size_t, char * *, size_t *), void *, const char *, int32_t);
```

两者 C 类型兼容，但文本不等（一个是 typedef，一个是内联函数指针）。如果 S0 按提案写「逐行相等」，当前代码就会**假红**。另外生成头第一行的 `/* Generated by ct … multi-file mode … */` 也不同。

### 7.3 建议的 S0 形态

1. 编号用 **⑱**（⑮/⑯/⑰ 已占用，[check.sh:630](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L630)、[:641](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L641)、[:656](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L656)）。
2. 每档生成到**独立目录**（避免 §6.4 的 basename 覆盖）。
3. 不 diff 文本，而是用 C 编译器做类型兼容：为每个 `js_backend_*` 写一个规范函数指针类型，分别包含每档头并赋值，开 `-Werror=incompatible-pointer-types`。类型不一致必须编译失败。
4. 负例：改一个参数类型 → ⑱ 必须红；不是只靠 quickjs 档的 `②b` 偶然红。
5. 同时比较 `nm -g --defined-only` 的 `js_backend_*` 符号集合（两档应相同）。

**判定：S0 必要且成立；提案的「逐行相等」测法与「⑮」编号不成立。**

---

## 8. CI 按档位条件化测试：null 档会掩盖 quickjs 失败

### 8.1 当前事实

- HEAD 没有 shell 宿主 smoke：`hosts/` 只有 README，`hosts/shell/` 为空（[hosts/README.md:9](file:///Users/samxx/trae-ws/nexcore/hosts/README.md#L9)）。所以「shell 宿主 smoke」当前**未验证**，没有可跑的夹具。
- 但同类条件化测试已经存在：⑧⑨/smoke（[check.sh:416-441](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L416-L441)）、⑭ JS module load（[:618-626](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L618-L626)）、⑮/⑯/⑰（[:630-700](file:///Users/samxx/trae-ws/nexcore/tools/check.sh#L630-L700)）都只在 quickjs 档真跑，null 档打印 SKIP/N/A/UNVERIFIED。

### 8.2 实测掩盖

在 `/tmp/nxeval` 把 quickjs 的 `pump` 循环改成 `while (0)`（只破坏 quickjs 档），两档各跑一次：

```text
$ NEXCORE_JS=null bash tools/check.sh
…
⑧ bootstrap_js executed: SKIP (NEXCORE_JS=null…)
⑨ drain -> sink: SKIP (NEXCORE_JS=null…)
smoke: SKIP (NEXCORE_JS=null…)
…
ALL CHECKS PASSED (NEXCORE_JS=null)
rc=0

$ NEXCORE_JS=quickjs bash tools/check.sh
…
⑮ FAIL: 微任务未被泵到（Promise 回调不可见）
rc=1
```

**null 档 rc=0，quickjs 档 rc=1。** 如果 CI 只把 null 档设为必过，quickjs 档的破坏（包括未来 shell 宿主 smoke 的失败）完全不可见。

### 8.3 结论与建议

**判定：掩盖风险成立。**

- CI 必须是 `NEXCORE_JS=null` 与 `NEXCORE_JS=quickjs` 两个**必过** job，不能把 quickjs 设为可选/仅 nightly。
- null 档的 SKIP 行应汇总打印（例如 `SKIPPED 3`），不要让 `ALL CHECKS PASSED (NEXCORE_JS=null)` 被读成「全部覆盖」。
- shell 宿主 smoke 只能在 quickjs 档真跑；null 档跳过是合理的，但必须由 quickjs job 覆盖，且 quickjs job 不能 `allow_failure`。
- 若保留「条件化」，至少加一条覆盖声明：两档 job 必须都出现在 CI 矩阵里，缺一即失败。

---

## 9. 复现清单

| 内容 | 位置 |
| --- | --- |
| CT 缺陷六组对照 | `/tmp/nxdef`，命令见 §1.2 |
| 当前两档全量检查与产物 | `/tmp/nxeval`、`/tmp/nxart/{null,quickjs}` |
| 干净 quickjs 档归档 | `/tmp/nxqjclean` |
| S1 原型（无 namespace 双后端） | `/tmp/nxs1` |
| 双 import 根歧义 | `/tmp/nxamb` |
| 同 basename 输出覆盖 | `/tmp/nxcollide` |
| 两份实现同名 C 符号重复链接 | `/tmp/nxdup` |
| 自造 ELF64 动态可执行 | `/tmp/mkelf.py`、`/tmp/dyn.elf` |
| P3 运行期 vtable | `/tmp/nxp3` |
| P4 跨单元 trait / 工厂 | `/tmp/nxp4` |

全部 `/tmp` 产物在评审后可删；仓库只新增本文。未修改 `src/`、`tools/`、`tests/`、`docs/decisions.md`、`docs/phases/PH-10/card.md`。

---

## 10. 对提案的最小修订要求

1. **改缺陷描述**：写成「带 `namespace` 的单元不能用静态固有方法作为构造入口」，不要写「固有方法跨单元不可用」。
2. **删掉「后端内部用 trait 必须加 shim」**：跨单元 trait impl 与自由函数工厂已验证可用；后端单元无 namespace，不受该缺陷影响。
3. **重挂基线**：以 `a1260c2` 或更新提交为准；`808fc53` 上 `src/js_backend/` 尚不存在，也没有两档切换。
4. **删掉 `turns()` 已删的说法**：它仍在 `units.json`、映射、两个实现和 deprecated 的 `js_host` 中；若确实要删，应先单独提案并同步映射/units/stat。
5. **重写判据 2**：归档用 `ar t` + `nm` 符号扫描；最终产物再用 `otool -L`/`objdump -p`。不要把 `otool -L` 直接用在 `.a` 上。
6. **保留同 sha 判据但降级**：它是「消费者零改动」的证据，不是后端等价的证据；补 S0 与 quickjs 行为测试。
7. **S1 写清 5 条 CT 约束**：无 namespace、同名单元不能双根、输出 basename 不能撞、`⑤a` self-import、`②a`/`⑤b` 接线；并说明它相对当前同目录切换的净收益。
8. **S0 用 ⑱，用 C 类型兼容而不是文本 diff**；每档独立输出目录，负例必须真红。
9. **CI 两档都必过**；null 档的 SKIP/N/A 不能当作 quickjs 档覆盖的替代。

---

## 11. 评审结论

- **P1 不采纳、公开 ABI 禁 vptr、S0 有必要、两档 `NexCore.c` 同 sha**：这四条成立；其中同 sha 已由实测确认，不再是推演。
- **CT 缺陷归因、基线现状、`otool -L libnexcore.a`、S0 的测法与编号**：这四处不成立，必须改。
- **S1 目录布局**：可行，但受 CT 的 namespace / import 根 / 输出 basename / `⑤a` 四条硬约束；它不是解决签名漂移的必要条件，当前同目录切换 + 新 S0 是更小的改动。
- **CI 条件化**：风险真实存在；null 档 rc=0 而 quickjs 档 rc=1 已实测复现，必须双档必过。
