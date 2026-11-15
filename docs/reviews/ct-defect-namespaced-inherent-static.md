# CT-DEF-001：命名空间单元内的固有静态方法调用无法解析

| 项 | 内容 |
| --- | --- |
| 编号 | CT-DEF-001 |
| 组件 | `ct` / `ctc`（编译器名字解析，非生成代码） |
| 版本 | `ct v0.44.2`、`ct v0.49.0`，行为一致 |
| 状态 | 已最小复现，待上游修复；**不是新回归** |
| 严重度 | 中：不阻塞本仓库现有构建（`src/` 未用固有静态方法），但封死「命名空间单元用 OO 语法做构造入口」这条路 |
| 复现目录 | [experiments/oo_shim/issue_inherent_cross_unit/](../../experiments/oo_shim/issue_inherent_cross_unit/) |
| 纪律 | 未改 `src/`、未改编译器、只本地提交；不 push、不打 tag |

---

## 1. 一句话结论

在**带 `namespace` 的单元**里，`impl Ring { static int open(int n) { … } }` 已经正确落码
`ring_Ring_open` 并写进生成头，但调用点写 `Ring.open(4)` 会在 CT 侧报
`use of an undeclared variable`。

**同一个单元里也报**；把 `namespace` 去掉，同单元和跨单元都能编过。
所以这不是「跨单元」问题，而是「命名空间 + 固有静态方法调用」问题。
原始怀疑（固有方法跨单元不可用）被下面的同一单元对照**证伪**。

---

## 2. 最小复现（两个单元；去注释与空行共 13 行，含注释 23 行）

A 单元 [ring/Ring.h.ct](../../experiments/oo_shim/issue_inherent_cross_unit/ring/Ring.h.ct)：

```ct
namespace ring;

pub struct Ring {
    int n;
}

impl Ring {
    pub static int open(int n)
    {
        Ring r;

        r.n = n;
        return r.n;
    }
}
```

B 单元 [use/use.ct](../../experiments/oo_shim/issue_inherent_cross_unit/use/use.ct)：

```ct
import ring::Ring;

int main(void)
{
    return Ring.open(4);
}
```

命令与原始输出（`ct v0.49.0`，逐字，含退出码）：

```text
$ ct build --emit-c --ns-root . -I . use/use.ct -o o1
use/use.ct:6:16: error: use of an undeclared variable
rc=1
```

注意：编译器只输出这一行，没有「N error(s) generated」之类的汇总。

### 2.1 方法本身是导出的（排除可见性问题）

单独构建 A 单元，生成头里有完整原型：

```text
$ ct build --emit-c --ns-root . ring/Ring.h.ct -o oh
$ cat oh/Ring.h
...
struct ring_Ring;
struct ring_Ring {
    int n;
};
int ring_Ring_open(int n);
```

也就是说：**方法已导出，符号名是 `ring_Ring_open`；失败发生在调用点的名字解析**，
不是 `pub` 漏写、不是头文件没生成、不是链接期问题。失败时 `-o` 目录为空——连
`Ring.c` 都没有生成，编译器在发射 C 之前就退出了。

---

## 3. 对照组

### 3.1 同一单元、同一命名空间 —— 仍然失败

[ring/same_unit.ct](../../experiments/oo_shim/issue_inherent_cross_unit/ring/same_unit.ct)
把 A 的声明/实现和 B 的调用放进同一个单元：

```text
$ ct build --emit-c --ns-root . ring/same_unit.ct -o o2
ring/same_unit.ct:20:16: error: use of an undeclared variable
rc=1
```

这一条直接推翻了「跨单元才失败」的假设。

### 3.2 正对照：去掉 namespace，同单元与跨单元都通过

同一单元、无 namespace（[flat_same/flat_same.ct](../../experiments/oo_shim/issue_inherent_cross_unit/flat_same/flat_same.ct)）：

```text
$ ct build --emit-c flat_same/flat_same.ct -o o5
cc -std=c99 -I o5 o5/flat_same.c -o a.out
rc=0
```

两个单元、无 namespace（[flat/Ring.h.ct](../../experiments/oo_shim/issue_inherent_cross_unit/flat/Ring.h.ct) +
[flat_use/flat_use.ct](../../experiments/oo_shim/issue_inherent_cross_unit/flat_use/flat_use.ct)）：

```text
$ ct build --emit-c -I flat flat_use/flat_use.ct -o o4
cc -std=c99 -I o4 o4/flat_use.c o4/Ring.c -o a.out
rc=0
```

正对照说明：**固有静态方法跨单元本身是支持的**，触发条件只有一个——单元声明了 `namespace`。

### 3.3 边界对照：命名空间内的实例方法正常

[ring/inst.h.ct](../../experiments/oo_shim/issue_inherent_cross_unit/ring/inst.h.ct) 提供
`pub int get()`，跨单元调用
（[inst_use/inst_use.ct](../../experiments/oo_shim/issue_inherent_cross_unit/inst_use/inst_use.ct)）：

```text
$ ct build --emit-c --ns-root . -I . inst_use/inst_use.ct -o o6
cc -std=c99 -I o6 o6/inst_use.c o6/inst.c -o a.out
rc=0
```

所以「命名空间 + 实例方法」没问题，出问题的只有「命名空间 + 静态方法」。

### 3.4 边界矩阵（只列实测过的格子）

| 单元有 `namespace` | 调用形态 | 同单元 | 跨单元 |
| --- | --- | --- | --- |
| 有 | `Type.method(...)`（静态） | **FAIL** | **FAIL** |
| 有 | `r.method(...)`（实例） | PASS（临时工程，未入目录） | PASS（§3.3） |
| 无 | `Type.method(...)`（静态） | PASS（§3.2） | PASS（§3.2） |
| 无 | `r.method(...)`（实例） | PASS（临时对照，未入目录） | 未验证 |

补充：`impl` 里**不写 `static`** 时，`open` 被当成实例方法，调用 `Ring.open(4)` 报
`'Ring' has no static method 'open'`（`ct v0.49.0`，`rc=1`），与本缺陷无关，但复现时别写错。

---

## 4. 负例：`impl` 里只写声明、不写函数体

[decl_only/decl_only.ct](../../experiments/oo_shim/issue_inherent_cross_unit/decl_only/decl_only.ct)：

```ct
struct Ring {
    int n;
}

impl Ring {
    static int open(int n);
}
```

原始输出（`ct v0.49.0`）：

```text
$ ct build --emit-c decl_only/decl_only.ct -o o3
decl_only/decl_only.ct:7:27: error: unexpected token
rc=1
```

`ct v0.44.2` 与 `v0.49.0` 完全一致。指向第 7 行第 27 列的 `;`。
含义：**固有方法必须自带函数体**，不能像 C 头文件那样只放原型；跨单元共享实现只能靠
`.h.ct` 里的完整方法体，或自由函数 + SHIM。

---

## 5. 影响面

### 5.1 走不通的 OO 路径

| 方案 | 结果 | 原因 |
| --- | --- | --- |
| `queue::Ring.open(cap)` 作为固有静态构造入口 | 编不过 | 本缺陷（`queue` 单元有 `namespace queue`）；同单元也不行 |
| `js_backend::Backend.create(...)` 作为 vtable 工厂入口 | 编不过 | 同上；工厂不能写成命名空间类型的静态方法 |
| 把内部 `struct` 发布成 `pub struct` + 静态工厂，消费者只持接口 | 入口即失败 | 失败点正是「消费者拿到对象」的第一步 |
| 命名空间类型 + 实例方法（`r.get()`） | 通过 | §3.3；但消费者必须持有带类型的接收者 |
| 无 namespace 的单元 + 静态方法 | 通过 | §3.2；但破坏「namespace == 目录」约定 |
| 自由函数 / SHIM 映射（现状） | 通过且零开销 | 不经过静态方法解析 |

关键点：**这不是「不能用 OO」，而是「不能用静态方法作为跨单元的构造入口」**。
实例方法可用，但 nexcore 的消费者手里只有 `void *` 不透明句柄（
[NexCore.ct:49-59](../../src/NexCore.ct#L49-L59)），要改成实例方法就得把 `struct` 布局发布到
接口头，和「句柄不透明」的 ABI 隔离相悖。

### 5.2 为什么 `queue` 的 push/pop 只能保持单实现

1. `queue` 单元声明了 `namespace queue`，所以任何 `impl Ring { static … }` 的入口都会命中本缺陷
   （[queue.h.ct](../../src/queue/queue.h.ct) 目前是自由函数 + [queue_map.m.ct](../../src/queue/queue_map.m.ct) 的静态映射）。
2. 消费者侧没有 `Ring` 实例，只有 `void *`；转实例方法要发布布局，破坏不透明句柄。
3. `push`/`pop` 是每条消息都走的热路径，且只有一份实现；换成 vtable 只增加间接跳转，
   不产生任何多态收益。
4. 即便把 `queue` 改成无 namespace 来绕开本缺陷，也会破坏仓库的「namespace == 目录」约定与
   ⑤a 的既有布局，收益为零。

结论：**队列保持 `queue::Ring.push/pop` 的 SHIM 单实现是正确选择**，但正确理由要改成
「命名空间 + 静态入口不可用 + 热路径单实现」，不是「跨单元固有方法不可用」。

### 5.3 次生问题：诊断误导

报错是 `use of an undeclared variable`，指向类型名 `Ring`，会让人先怀疑 import、
`pub` 或 `namespace` 目录不匹配；但方法其实已经导出。建议上游至少把文案改成
`static inherent method on a namespaced type is not supported` 之类，并指到
规范 §4.11.1 / §4.60。

---

## 6. 不是新回归（请勿按近期引入去 bisect）

同一份复现文件，`ct v0.44.2` 与 `ct v0.49.0` 输出逐字相同：

```text
# ct v0.44.2
$ ct build --emit-c --ns-root . -I . use/use.ct -o p1
use/use.ct:6:16: error: use of an undeclared variable
rc=1

$ ct build --emit-c --ns-root . ring/same_unit.ct -o p2
ring/same_unit.ct:20:16: error: use of an undeclared variable
rc=1

$ ct build --emit-c decl_only/decl_only.ct -o p3
decl_only/decl_only.ct:7:27: error: unexpected token
rc=1

$ ct build --emit-c -I flat flat_use/flat_use.ct -o p4
cc -std=c99 -I p4 p4/flat_use.c p4/Ring.c -o a.out
rc=0
```

```text
# ct v0.49.0
$ ct build --emit-c --ns-root . -I . use/use.ct -o o1
use/use.ct:6:16: error: use of an undeclared variable
rc=1

$ ct build --emit-c --ns-root . ring/same_unit.ct -o o2
ring/same_unit.ct:20:16: error: use of an undeclared variable
rc=1

$ ct build --emit-c decl_only/decl_only.ct -o o3
decl_only/decl_only.ct:7:27: error: unexpected token
rc=1

$ ct build --emit-c -I flat flat_use/flat_use.ct -o o4
cc -std=c99 -I o4 o4/flat_use.c o4/Ring.c -o a.out
rc=0
```

- 固有静态方法（Phase 29 补丁）与「`pub` 只允许在 `.h.ct` 的 `pub struct` 固有 impl 上」
  （Phase 42，v0.42.0）都早于 v0.44.2，本现象在此区间两端一致。
- **不要**在 nexcore 的提交历史里 bisect；也**不要**假定是 v0.44.2 → v0.49.0 之间引入。
  若要定位 ct 侧，方向是「静态固有方法调用点的命名空间解析」，不是近期改动。

---

## 7. 修法方向（只登记，不实施）

1. **解析**：调用点的裸类型名 `Ring` 先按「当前命名空间（同单元）」或「导入表（跨单元）」
   解析成 `ring_Ring`，再拼静态方法名 `ring_Ring_open`。
2. **诊断**：解析失败时区分「类型不存在」与「命名空间类型的静态方法未支持」，别报
   `use of an undeclared variable`。
3. **规范对齐**：§4.11.1 已写明 `pub struct` 的 `pub` 固有方法跨单元可达，
   §4.60 写明 `pub` 只在 `.h.ct` 的 `pub struct` 固有 impl 上生效；当前实现与规范不一致。

上面第 1 条是**从现象推的假设**，未读编译器源码，未验证。

---

## 8. 复现命令（在复现目录内）

```sh
cd experiments/oo_shim/issue_inherent_cross_unit
mkdir -p /tmp/ctdef/{o1,o2,o3,o4,o5,o6}

# 1) 跨单元 + namespace：FAIL
ct build --emit-c --ns-root . -I . use/use.ct -o /tmp/ctdef/o1

# 2) 同单元 + namespace：FAIL（证明不是跨单元）
ct build --emit-c --ns-root . ring/same_unit.ct -o /tmp/ctdef/o2

# 3) 声明无函数体：FAIL
ct build --emit-c decl_only/decl_only.ct -o /tmp/ctdef/o3

# 4) 跨单元、无 namespace：PASS
ct build --emit-c -I flat flat_use/flat_use.ct -o /tmp/ctdef/o4

# 5) 同单元、无 namespace：PASS
ct build --emit-c flat_same/flat_same.ct -o /tmp/ctdef/o5

# 6) 跨单元 + namespace + 实例方法：PASS
ct build --emit-c --ns-root . -I . inst_use/inst_use.ct -o /tmp/ctdef/o6
```

环境：macOS，`ct v0.44.2`（`PATH="$HOME/.local/ct/0.44.2:$PATH"`）与 `ct v0.49.0`
（系统 `ct`）各跑一遍；输出见 §6。所有 `-o` 目录需预先存在，否则报
`ctc: cannot write the output file`（与本缺陷无关）。

---

## 9. 未验证 / 边界

- 只测了 `int` 与方法名 `open`；未穷举其它返回类型、参数个数、泛型 `impl<T>`。
- 未读 `ctc` 源码，§7 的修法方向是现象推断。
- 实例方法的「无 namespace + 跨单元」格子未测。
- 本缺陷单只覆盖编译器行为；`src/` 未改动，现有两档构建不受影响（它们不用固有静态方法）。
