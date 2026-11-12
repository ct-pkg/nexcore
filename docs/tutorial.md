# ct-NexCore 使用教程（基于仓库现状）

> 本文面向第一次接触本项目的人。所有命令都在本仓库根目录实跑验证过；
> 文中标注「已实现 / 骨架 / 仅文档」的地方，均以仓库里的实际代码为准，不按设计文档的完成度来写。
>
> 验证环境：macOS arm64、`ct v0.19.1`、Apple clang、Python 3.15、Swift 6.x、`experiments/third_party/quickjs/libquickjs.a` 已就绪。

---

## 1. NexCore 是什么

ct-NexCore 是一个**跨端业务核心复用框架**，不是业务库。它夹在两块**宿主项目**的代码之间：

```
宿主原生 UI（SwiftUI / Compose）  ←→  ★ ct-NexCore ★  ←→  宿主业务 JS（跑在 QuickJS 里）
                                        ↑
                                 宿主业务 Core（CT/C，经生成的 registry 注册）
```

框架只负责四件事：**把调用送到、把结果送回、把错误报清、把边界锁死**。它承诺交付的能力是：

| 交付物 | 现状 |
|---|---|
| C ABI（7 个函数 + 冻结的 `NexCoreConfig`） | **已有可编译实现**（骨架语义） |
| JSON 协议（信封/错误码/订阅/超时/取消） | **仅文档**（`docs/protocol.md`） |
| QuickJS 运行时集成 | **仅实验代码**（`experiments/src/bridge.ct`，未进 `src/`） |
| 代码生成器 CLI | **已有骨架**（`tools/nexcore_gen.py`，只生成映射单元） |
| 结构化调试日志 | **部分**（错误码与统计键已在 ABI，采样未实现） |

**解决什么问题**：同一套业务逻辑（业务 JS + 业务 Core）在 macOS/iOS/Android 三个原生 UI 上复用，而不为每个平台重写一遍；同时把「跨语言边界」限定在一份可逐行审查的固定模式里，让 AI 可以大量生成宿主侧代码但无法越过线程/生命周期边界。

**不解决什么**：不做业务规则、不做 UI、不做通用 RPC、v1 不做热更新（JS 只随包内置）。

---

## 2. 现状速查表（先看这张，别被文档语气误导）

| 模块 | 目录 | 状态 | 判据 |
|---|---|---|---|
| ABI 头与 7 个函数 | `src/NexCore.h.ct` + `src/NexCore.ct` | **骨架可运行** | `nm` 断言 7/7 为 `T` 符号；`tests/smoke.c` 全过 |
| 入站队列 | `src/queue/` | **骨架** | 有 `open/push/depth/close`，**没有 `pop`**、无锁、无 condvar |
| QuickJS 宿主 | `src/js_host/` | **占位** | `init()` 只把字符串 `"bootstrap"` 压进队列，**从不读 `bootstrap_js`** |
| 协议单元 | `src/protocol/` | **死代码** | 定义了 2 个校验函数，但 `NexCore.ct` 不 import、不调用 |
| 映射单元与生成器 | `src/*/*_map.m.ct`、`tools/nexcore_gen.py` | **可用** | `--check` 无漂移；④ 校验 7 条 recipe |
| 机械检查 | `tools/check.sh` | **可用** | `SCRIPT_RC=0` |
| 运行时（JS 线程、turn、超时、关闭） | — | **未开始** | `src/` 无 pthread、无 `JS_*`、无 pending map |
| 平台接入（Swift/Kotlin/transport） | — | **未开始** | 无 `platform/`、无 `bridge/`、无绑定 |
| Schema 与四端代码生成 | — | **未开始** | 无 `schema/`、无 `generated/`、无 `tests/fixture.schema.yaml` |
| 能力验证与探针 | `experiments/` | **可复现** | `experiments/build.sh` 全绿，覆盖 QuickJS 双向调用、Promise、ABI、布局、缺口 |

一句话：**`src/` 完成的是「ABI 形状 + 参数校验 + 单元拓扑」，不是运行时；`experiments/` 证明的是「CT 能嵌 QuickJS」，不是「框架能跑业务」。**

---

## 3. 目录结构

```
ct-NexCore/
├── docs/                  # 契约与边界（设计、协议、线程、生成、验收、决策、待确认、教程）
├── reviews/               # 历次独立评审报告（能力审计、四轮设计复核、骨架复核）
├── experiments/           # CT ↔ QuickJS 地基验证与语言缺口探针（不是正式骨架）
│   ├── build.sh           # 全部验证的一键入口
│   ├── src/               # bridge.ct / abi.h.ct / layout.ct / crossunit_*.ct
│   ├── js/bridge_test.js  # 异步与错误路径脚本
│   ├── host/              # C 与 Swift 消费方
│   ├── gaps/              # 语言缺口探针与绕法
│   ├── third_party/quickjs/  # 本地 QuickJS（已 gitignore）
│   └── out/               # 全部实测产物（已 gitignore）
├── src/                   # ★ 正式骨架（唯一被认为是「实现」的目录）
│   ├── NexCore.h.ct       # 根 ABI 单元：pub enum 常量 + struct NexCoreConfig + 7 个函数
│   ├── NexCore.ct         # 根实现：ABI 入口、handle 生命周期、参数校验
│   ├── units.json         # 生成器输入：namespace / trait / source / methods
│   ├── queue/             # namespace queue：queue.h.ct / queue.ct / queue_map.m.ct(生成物)
│   ├── js_host/           # namespace js_host：同上
│   └── protocol/          # namespace protocol：只有 .h.ct / .ct（无 map，无人调用）
├── tools/
│   ├── check.sh           # 构建 + 机械检查的唯一入口（编码侧唯一维护）
│   ├── nexcore_gen.py     # 映射单元生成器 + 漂移检查 + ④ 符号校验
│   └── scan_c.py          # ③ 断言生成 C 里没有 '::'
├── tests/
│   ├── smoke.c            # 端到端冒烟：create→send→stat→shutdown→free
│   └── stat_keys.c        # ⑥ 统计键契约
├── abi/expected-forward-decls.txt   # 发布头前置声明 golden（唯一允许 struct NexCoreConfig;）
└── build/                 # 构建产物（abi / impl / bin），可随时删除重建
```

两条容易踩的规则：

1. **命名空间路径 = 目录路径**。`namespace queue;` 只能出现在 `src/queue/` 下，不能平铺成 `src/queue.h.ct`。
2. **`.m.ct` 是生成物**，由 `tools/nexcore_gen.py` 从 `units.json` 生成，不要手改（`①`-漂移检查会失败）。

---

## 4. 前置依赖与工具链

| 依赖 | 版本/用途 | 检查方式 |
|---|---|---|
| `ct` | **v0.19.1**（仓库实测基线 `c27da3d`）。CT 迭代快，换版本必须重跑 `tools/check.sh` 与 `experiments/build.sh` | `ct v0.19.1` |
| C 编译器 | 严格 `-Wall -Wextra -Werror` 必须零警告 | `cc --version` |
| Python 3 | 生成器与检查脚本，仅用标准库（无 PyYAML 依赖） | `python3 --version` |
| bash | `tools/check.sh` 用 `set -euo pipefail` | `bash --version` |
| `curl` / `tar` / `make` | 仅在缺失 QuickJS 时自动下载编译 | — |
| `swiftc` | 可选；`experiments/build.sh` 用它验证 Swift 侧消费 ABI | `command -v swiftc` |

注意两个**必须遵守**的构建事实：

- 生成完整头文件只能用 `ct build --emit-c`。`ct compile` 产出的 `.h` 只有一个 include guard，`pub` 函数原型不会出现。
- `ct build -o <dir>` 要求**输出目录已存在**，否则报 `ctc: cannot write the output file`。

---

## 5. 五分钟跑通现有链路

### 5.1 一条命令跑全部检查

```bash
cd /Users/samxx/vscspace/ct-NexCore
bash tools/check.sh
```

预期结尾：

```text
ALL CHECKS PASSED
```

`check.sh` 会先 `rm -rf build/abi build/impl build/bin` 再重建，所以它总是从头开始。它依次做这些事：

| 编号 | 检查什么 | 为什么要有 |
|---|---|---|
| ⓪ | `units.json` 与已提交的 `*_map.m.ct` 逐字节一致 | 防止手改生成物 |
| ①a | Pass 1 只编 `NexCore.h.ct`，发布头非空 | 发布头唯一来源 |
| ①b | 发布头里除 `NexCoreConfig` 外无内部 struct 名 | 防止内部类型泄漏进 ABI |
| ①c | 与 `abi/expected-forward-decls.txt` 逐行 diff | ABI 表面变更必须显式确认 |
| ②a | Pass 2 编 4 个实现单元，每个 `.c` 非空 | 缺 `import <self>` 会让 CT 静默丢命名空间前缀 |
| ②b | 每个 `.c` 过 `cc -Werror`（含 implicit-function-declaration） | 兜住裸调用错译与漏原型 |
| ②c | `nm` 断言 7 个 `nexcore_*` 都是 `T` 符号 | 防止函数被吞成 `static` |
| ③ | 生成的 C 里没有 `::`（先剥注释与字面量） | 防止 CT 命名空间语法漏进 C |
| ④ | 每条 map recipe 的 callee == 符号表 cname，且符号真实存在 | 三处同步（头/recipe/调用点） |
| ⑤a | 每个实现 `.ct` 第一条 import 必须是自身 | 同上，CT 静默失败的兜底 |
| ⑤b | `units.json` 的 `methods` 与 `.h.ct` 的 `pub` 函数集合相等 | 防止漏生成 recipe |
| ⑥ | 统计键 10..14 → `NX_E_INTERNAL` 且不写 `*out`；0/≥19 → `NX_E_INVALID_ARGUMENT` | 编号冻结 |
| smoke | `create → send → stat → shutdown → free` 全链路 | 拓扑真的能跑 |

> `build/` 下如果还有 `build/ct/` 这种旧目录，属于历史残留（当前脚本只写 `abi/`、`impl/`、`bin/`）。想要完全干净：`rm -rf build` 再跑一次。

### 5.2 手工分步（理解每一段在做什么）

```bash
# 0) 生成物漂移检查（等价于 CI 的单步）
python3 tools/nexcore_gen.py --spec src/units.json --src src --check

# 1) Pass 1：只编 ABI 头单元 → 唯一发布头
mkdir -p build/abi
ct build --emit-c --ns-root src src/NexCore.h.ct -o build/abi
head -20 build/abi/NexCore.h     # 只看得到 ABI 类型与 7 个函数

# 2) Pass 2：编全部实现单元 → 实现中间物
mkdir -p build/impl
ct build --emit-c --ns-root src -I src \
  src/NexCore.ct src/queue/queue.ct src/js_host/js_host.ct src/protocol/protocol.ct \
  -o build/impl

# 3) 严格编译每个实现 .c
cc -std=c99 -Wall -Wextra -Werror -Werror=implicit-function-declaration \
   -Werror=incompatible-pointer-types \
   -I build/abi -I build/impl -I src/js_host -I src/queue -I src/protocol \
   -fsyntax-only build/impl/NexCore.c

# 4) 编译并运行冒烟（消费侧 include 顺序：build/abi 必须在首位）
mkdir -p build/bin
cc -std=c99 -Wall -Wextra -Werror \
   -I build/abi -I build/impl -I src/js_host -I src/queue -I src/protocol \
   tests/smoke.c build/impl/*.c -o build/bin/smoke
./build/bin/smoke
```

`./build/bin/smoke` 的实测输出（含义见括号）：

```text
config: sizeof=88 v1=72 min=72 current=88 abi=1 protocol=1   （尾字段 log/log_user 让 CURRENT 比 V1 大 16）
create rc=0 handle=non-null
stat(after create) rc=0 depth=1        （create 时 js_host 把 "bootstrap" 压进队列，所以是 1）
send rc=0
stat(after send) rc=0 depth=2
drain rc=0 count=0                     （★ 骨架没有出站队列，drain 永远拿不到消息）
stat(LAST_ERROR=18) rc=0 v=0
stat(reserved 10) rc=9                 （9 = NX_E_INTERNAL：已分配未实现）
stat(next_free=19) rc=1                （1 = NX_E_INVALID_ARGUMENT：未分配）
shutdown rc=0
send(after shutdown) rc=3              （3 = NX_E_SHUTDOWN）
free rc=0
SMOKE OK
```

### 5.3 用 C 写一个最小调用方（可直接编译运行）

```c
/* nx_min.c —— 最小宿主：create → 发一条 JSON → 收统计 → 关闭 */
#include <stdio.h>
#include <string.h>
#include "NexCore.h"

int main(void)
{
    struct NexCoreConfig cfg;
    NexCoreHandle h = 0;
    int64_t v = 0;
    int32_t rc;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);      /* 最小长度协商，不是严格相等 */
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.bootstrap_js = "(function(){})";
    cfg.bootstrap_len = strlen(cfg.bootstrap_js);

    rc = nexcore_create(&cfg, &h);
    printf("create=%d\n", (int)rc);
    if (rc != NX_OK) return 1;

    const char *req = "{\"id\":1,\"method\":\"app.ping\",\"params\":{}}";
    printf("send=%d\n", (int)nexcore_send(h, req, strlen(req)));

    printf("inbox=%d\n", (int)nexcore_stat(h, NX_STAT_INBOX_DEPTH, &v));
    printf("depth=%lld\n", (long long)v);

    printf("shutdown=%d\n", (int)nexcore_shutdown(h));
    printf("free=%d\n", (int)nexcore_free(h));
    return 0;
}
```

编译运行：

```bash
cc -std=c99 -Wall -Wextra -Werror -I build/abi nx_min.c build/impl/*.c -o build/bin/nx_min
./build/bin/nx_min
```

输出：

```text
create=0
send=0
inbox=0
depth=2
shutdown=0
free=0
```

注意：**这里没有任何消息被 JS 处理，也没有任何消息通过 sink 返回**。`depth=2` 只说明两条字符串躺在队列里。这就是当前的真实上限。

---

## 6. 生成器与映射单元（b′ 拓扑）

CT 的命名空间单元不能直接互相调用，必须经过 `<ns>_map.m.ct` 映射单元。映射单元由生成器从 `src/units.json` 产出：

```json
{
  "units": [
    { "namespace": "queue",   "trait": "Ring", "source": "queue/queue.h.ct",   "methods": ["open","push","depth","close"] },
    { "namespace": "js_host", "trait": "Host", "source": "js_host/js_host.h.ct","methods": ["init","shutdown","turns"] }
  ]
}
```

产出的 `src/queue/queue_map.m.ct` 形如：

```text
#include "queue.h"

namespace queue;

pub trait Ring {
    void * open(int32_t capacity);
    ...
}

map trait Ring for namespace queue {
    open(capacity) -> queue_open(capacity);
    ...
}
```

调用方（`src/NexCore.ct`）的写法是：

```c
import NexCore;
import queue::queue_map;
import js_host::js_host_map;

q = queue::Ring.open(NEXCORE_DEFAULT_MAX_PENDING);
rc = js_host::Host.init(q, (int32_t)cfg->bootstrap_len);
```

三条命令的用途：

```bash
# 重新生成（会写盘，平时不需要）
python3 tools/nexcore_gen.py --spec src/units.json --src src

# 只比对不写盘；有漂移返回 2（CI 用）
python3 tools/nexcore_gen.py --spec src/units.json --src src --check

# 检查 ④：recipe 的 callee 必须等于符号表 cname，且符号出现在 CT 导出的头里
python3 tools/nexcore_gen.py --spec src/units.json --src src --verify-ct build/impl
```

**新增一个可跨单元调用的命名空间**（这是当前唯一可完整走通的「扩展流程」）：

1. 建目录 `src/<ns>/`，写 `<ns>.h.ct`（带 `namespace <ns>;` 与 `pub` 函数）和 `<ns>.ct`（第一行 `import <ns>;`）；
2. 把该单元追加到 `src/units.json` 的 `units` 数组；
3. `python3 tools/nexcore_gen.py --spec src/units.json --src src` 生成 `<ns>_map.m.ct`；
4. 把 `src/<ns>/<ns>.ct` 追加进 `tools/check.sh` 的 `IMPL_UNITS`（该文件由编码侧维护）；
5. 调用方 `import <ns>::<ns>_map;` 并写 `<ns>::<Trait>.<method>`；
6. `bash tools/check.sh` 全绿。

---

## 7. 跑实验代码：CT 到底能不能嵌 QuickJS

```bash
bash experiments/build.sh
```

预期结尾 `ALL CHECKS PASSED`（本机实测通过）。QuickJS 缺失时会自动 `curl` 下载 `quickjs-2025-09-13` 并 `make libquickjs.a`；本仓库当前已就绪，不会触发下载。

它验证的五件事：

| 段 | 跑的是什么 | 通过判据 |
|---|---|---|
| 1 | `experiments/src/bridge.ct` + `js/bridge_test.js` | JS→native 同步调用、CT→JS 反向调用、Promise resolve/reject、三条错误路径、干净关闭 |
| 2 | `experiments/src/abi.h.ct` | 导出原型/typedef 出现在 `abi.h`；C 与 Swift 两个宿主都得到 `add=42`、回调可用；`nm` 可见全局符号 |
| 3 | `experiments/src/layout.ct` vs `host/layout_ref.c` | `sizeof`/`offsetof` 逐行 `diff` 为空 |
| 4 | `experiments/src/crossunit_*.ct` | `.h.ct` 与普通 `.ct` 作 importer 的跨单元 import 都能跑 |
| 5 | `experiments/gaps/` | 三个缺口**按预期失败**（compile 模式 import、`errno`、文件内 `#ifdef`），三个绕法成功，`import` 写在 `namespace` 之后被静默丢弃 |

手动只跑其中一段：

```bash
# 只跑 QuickJS 桥接实验
ct compile experiments/src/bridge.ct -o experiments/out/bridge.c
cc -Wall -Wextra -Wno-unused-parameter -I experiments/third_party/quickjs \
   experiments/out/bridge.c experiments/third_party/quickjs/libquickjs.a \
   -lm -lpthread -o experiments/out/bridge
./experiments/out/bridge experiments/js/bridge_test.js
```

**这段实验代码不能直接当框架用**：它是单文件、轮询式 `usleep(1ms)` 泵、没有 ABI 分层、没有关闭协议。它证明的是「CT 语法与链接能力够用」，不是「Bridge 已存在」。

---

## 8. ABI 使用手册（当前可用的部分）

### 8.1 唯一跨 ABI 结构体

```c
struct NexCoreConfig {
    uint32_t struct_size;          /* 填 sizeof(struct NexCoreConfig) */
    uint32_t abi_version;          /* 必须 == NEXCORE_ABI_VERSION(1) */
    uint32_t protocol_version;     /* 必须 == NEXCORE_PROTOCOL_VERSION(1) */
    uint32_t default_timeout_ms;   /* 骨架未使用 */
    uint32_t max_message_bytes;    /* 0 → 默认 1 MiB；当前只做上限校验 */
    uint32_t max_pending;          /* 骨架未使用（队列固定 1024） */
    uint32_t flags;                /* bit0 debug；其余必须为 0 */
    uint32_t reserved;             /* 必须为 0 */
    int64_t (*now_ms)(void *user); /* 骨架未使用 */
    void *clock_user;
    const char *schema_hash;       /* 骨架未校验 */
    const char *bootstrap_js;      /* 骨架不读取内容，只取长度 */
    size_t bootstrap_len;
    NexCoreLog log;                /* 可选尾字段，骨架未调用 */
    void *log_user;
};
```

硬规则（写错会静默出错，不是编译报错）：

- **不要** `typedef struct NexCoreConfig NexCoreConfig;` —— CT 侧没有别名，C 侧一律写 `struct NexCoreConfig`。
- **不要**给它加 `use Trait` —— 会追加隐藏 vptr，Swift/Kotlin 已编译布局静默错位。
- `struct_size` 是**最小长度**：`>= NEXCORE_CONFIG_MIN_SIZE`(72) 即可；库忽略尾部未知字段。旧调用方传 72 也能用。
- 常量都是 `pub enum`，不是 `#define`（`#define` 不进导出头）。

### 8.2 七个函数与当前真实行为

| 函数 | 骨架行为 |
|---|---|
| `nexcore_create` | 校验 6 个字段 → 开队列 → 压一条 `"bootstrap"` → 分配 handle。**不启动 QuickJS、不建线程、不跑 bootstrap** |
| `nexcore_set_sink` | 只记录指针，**从不调用** |
| `nexcore_send` | 校验 handle/长度 → 复制字符串入队；满则 `NX_E_BACKPRESSURE` |
| `nexcore_drain` | 恒返回 `NX_OK` 且 `*out_count=0`，忽略 `max_wait_ms`，**不调用 sink** |
| `nexcore_shutdown` | 只把 `state` 置 SHUT，**不 join、不关闭 JS** |
| `nexcore_free` | 要求已 `shutdown`，否则 `NX_E_BUSY`；释放队列与 handle |
| `nexcore_stat` | 只有 key 1（inbox depth）与 18（last error）有真实值；2..9、15..17 恒 0；10..14 返回 `NX_E_INTERNAL`；其余 `NX_E_INVALID_ARGUMENT` |

返回码（数值已冻结，不得重排）：`0 OK / 1 INVALID_ARGUMENT / 2 INVALID_HANDLE / 3 SHUTDOWN / 4 BACKPRESSURE / 5 PAYLOAD_TOO_LARGE / 6 REENTRANT / 7 THREAD / 8 SCHEMA_MISMATCH / 9 INTERNAL / 10 BUSY / 11 CLOSE_TIMEOUT`。

---

## 9. 最小上手路径：以现状最多能跑到哪一步

**能跑到的终点**（本教程已验证）：

```
C 宿主编译 → create → send(原始字符串入队) → stat 读到 depth → shutdown → free
                                    ✗
                          到此为止：没有任何 JS 执行，没有任何消息回到宿主
```

再往下（把 `{"id":1,"method":"app.ping"}` 真的跑成一条响应）缺 6 件事，缺一件都跑不通：

| # | 缺口 | 现状证据 |
|---|---|---|
| 1 | **出站队列 + `pop` + condvar** | `src/queue/queue.h.ct` 只有 `open/push/depth/close`；`nexcore_drain` 恒 `count=0` |
| 2 | **JS 线程与 QuickJS 生命周期** | `src/` 无 `pthread`、无 `JS_*`；`js_host.init` 不读 `bootstrap_js` |
| 3 | **turn：解析 → 校验 → dispatch → 泵微任务** | `src/NexCore.ct` 不 import `protocol`，协议函数是死代码 |
| 4 | **pending / 超时 / 取消 / 晚到丢弃** | `default_timeout_ms`、`now_ms` 未被读取；无 pending map |
| 5 | **两阶段关闭序列（grace 500 + hard 200、join、DEAD 单点）** | `nexcore_shutdown` 只改一个状态位 |
| 6 | **sink 投递线程** | `nexcore_set_sink` 存的指针无人调用 |

按依赖顺序的推荐顺序就是上表 1→6（先队列，后线程，再 turn，再出站，再关闭，最后统计与验收）。平台接入（Swift/Kotlin/JNI/transport）在 6 之后。

已有的可复用资产：**b′ 拓扑与生成器、7 函数 ABI 形状、两趟构建、全部机械检查、`experiments/` 里已验证过的 QuickJS 调用形状（含 `JS_SetContextOpaque` 传上下文、`pthread` worker 只写纯数据）**。这些不需要重做。

---

## 10. 常见问题

**Q：`ct build` 报 `namespace 'x' does not match the directory ''`**
目录名必须等于命名空间，且要带 `--ns-root src`。`src/queue/queue.ct` 里写 `namespace queue;`。

**Q：CT 编译成功、`cc` 报 `implicit declaration of function`**
两种典型原因：① 实现单元漏了第一行 `import <self>;`（CT 会 `exit 0` 静默通过，但函数丢掉命名空间前缀）；② `import` 写在 `namespace` 之后（include 被静默丢弃）。`check.sh` 的 ⑤a 与 ②b 就是拦这两个。

**Q：想直接调用另一个命名空间的函数，怎么都不对**
不允许裸名或限定名直调。必须生成 `<ns>_map.m.ct`，调用方 `import <ns>::<ns>_map;`，写成 `<ns>::<Trait>.<method>`。取地址（`&f`）跨单元同样不行，必须在本单元生成 `static` wrapper。

**Q：`build/impl/NexCore.h` 和 `build/abi/NexCore.h` 不一样，用哪个**
只有 `build/abi/NexCore.h` 是对外发布头。`build/impl/*.h` 是同一次构建的中间物，含内部 struct 前置声明，**不得安装、不得被平台或测试 include**。

**Q：改了 `.h.ct` 里某个 `pub` 函数签名，为什么检查失败**
要同步三处：目标 `.h.ct`、`src/units.json` 的 `methods`（⑤b）、以及调用点。然后重新生成 map（⓪ / ④）。

**Q：`nexcore_stat` 返回 9 是什么意思**
`NX_E_INTERNAL`，表示该键「已分配但未实现」（10..14）。这是设计要求：不能返回 0，否则平台会把「未实现」读成「真实观测值 0」。

**Q：文档说 `#ct-raw` 主动不采用，但 `src/NexCore.ct` 里用了**
现状确实如此：`#ct-raw` 只用来承载 `_Static_assert`（因为 CT 会丢弃 `.h.ct` 里的静态断言），没有用来绕符号解析。口径待统一，属于已知文档/实现偏差，不影响构建。

**Q：`units.json` 里为什么没有 `protocol`**
`protocol` 单元目前无人调用、也没有映射单元，所以没进生成器清单。副作用是 ⑤b 的一致性检查不覆盖它——新增第 3 个可调用命名空间时记得补进去。

---

## 11. 术语与延伸阅读

| 术语 | 含义 |
|---|---|
| 信封 | 跨 UI↔JS 边界的 JSON 对象：`{id, method, params}` / `{id, result\|error}` |
| turn | JS 线程处理一条入站消息 + 执行 + 泵微任务 + 超时判定的一个完整单元 |
| 映射单元 | `<ns>_map.m.ct`，把 `<ns>::Trait.method` 落成 C 符号 `ns_method` 的声明式桥 |
| 固定模式 | Bridge 中不随业务变化、可逐条审查的 16 条约束（FIX-01..16） |
| 两趟构建 | Pass 1 只出发布头；Pass 2 出实现。合成一趟会污染发布头 |

按顺序读：[design.md](design.md) → [protocol.md](protocol.md) → [async-threading.md](async-threading.md) → [codegen.md](codegen.md) → [ai-friendly.md](ai-friendly.md) → [roadmap.md](roadmap.md) → [acceptance.md](acceptance.md) → [decisions.md](decisions.md) → [open-questions.md](open-questions.md)。
想知道「哪些结论被实测推翻过」，读 [../reviews/](../reviews/) 下的四轮复核与能力审计。
