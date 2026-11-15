# experiments/oo_shim/ —— CT 两套 OO 机制的最小实测

> 只读探针：不改 `src/`，不做产品代码改造。基线编译器 `ct v0.44.2`
> （`PATH="$HOME/.local/ct/0.44.2:$PATH"`；系统 `ct` 为 v0.49.0，仅在 P5 复跑过一项）。
> 每个 `pN_*` 都是可独立重建的最小工程，源码全部在内，生成物不提交。
>
> 结论汇总见 [docs/reviews/nexcore-oo-plan.md](../../docs/reviews/nexcore-oo-plan.md)。

## P1 `p1_two_maps` — 两份映射单元，同一 import 路径

结构：`null/jb/jb_map.m.ct` 与 `quick/jb/jb_map.m.ct` 声明**同名 trait、同 namespace**，
recipe 分别指向 `jb_null_*` 与 `jb_qjs_*`；消费者 `app/app.ct` 只写 `import jb::jb_map;`。

```sh
cd p1_two_maps
ct build --emit-c --ns-root null null/jb_null/jb_null.h.ct -o out
ct build --emit-c -I null -I out app/app.ct null/jb_null/jb_null.ct -o out
# app.c: jb_null_open / jb_null_eval / jb_null_turns / jb_null_close

ct build --emit-c --ns-root quick quick/jb_qjs/jb_qjs.h.ct -o out
ct build --emit-c -I quick -I out app/app.ct quick/jb_qjs/jb_qjs.ct -o out
# app.c: jb_qjs_open / jb_qjs_eval / jb_qjs_turns / jb_qjs_close
```

实测：两次都 rc=0，消费者源码一字未改，切档只改 `-I` 根与实现单元。

**负例（关键）**：两个映射根同时进 `-I` 时不是静默遮蔽，而是硬错：

```sh
ct build --emit-c -I null -I quick -I out app/app.ct ... 
# app/app.ct:4:1: error: unit 'jb/jb_map' exists in two import roots:
#   'null/jb/jb_map.m.ct' and 'quick/jb/jb_map.m.ct'
```

所以「同一接口名挂两份映射」可行，但每次构建必须**只挂一个**映射根。

## P2 `p2_stable_map` — 单份稳定映射 + 每档独立实现单元（推荐形态）

结构：`maps/jb/jb_map.m.ct` 只有一份，recipe 指向固定 C 符号 `js_backend_*`；
`impl_null/` 与 `impl_quick/` 各有一对无 namespace 的 `jb_backend.ct` / `.h.ct`，
用**同样的 C 符号名**提供两套实现。

```sh
cd p2_stable_map
ct build --emit-c impl_null/jb_backend.h.ct -o out_null
ct build --emit-c -I maps -I out_null app/app.ct impl_null/jb_backend.ct -o out_null
cc -std=c99 -Wall -Wextra -Werror -I out_null out_null/app.c out_null/jb_backend.c -o app_null
./app_null                                   # eval=-1 turns=0

ct build --emit-c impl_quick/jb_backend.h.ct -o out_quick
ct build --emit-c -I maps -I out_quick app/app.ct impl_quick/jb_backend.ct -o out_quick
cc -std=c99 -Wall -Wextra -Werror -I out_quick out_quick/app.c out_quick/jb_backend.c -o app_quick
./app_quick                                  # eval=42 turns=1

diff out_null/app.c out_quick/app.c          # 无输出：消费者生成 C 逐字节相同
```

实测：两档的 `app.c` 完全相同；只有实现单元与 `-I` 不同。这是把 SHIM 用在档位边界上的
最干净形态：契约一份、实现两份、消费者零改动。

## P3 `p3_vtable_runtime` — vtable 运行期选择

`main.ct` 里两个 struct `use Backend`，`impl Backend for ...` 各写一套；
接口值存进 `struct Handle { Backend js; }`（堆上），按 `argv[1]` 在运行期赋值。

```sh
cd p3_vtable_runtime
mkdir -p out
ct build --emit-c main.ct -o out
cc -std=c99 -Wall -Wextra -Werror -I out out/main.c -o app
./app          # turns=0 eval=-1
./app quick    # turns=1 eval=42
```

实测：同一进程/同一二进制内运行期切换实现成立；生成 C 里是
`h->js.vt->turns(h->js.self)`。
限制：`h->js` 是 `struct Backend_iface`，不能写 `h->js.tag`（接口值只有 `self`/`vt`）。

## P4 `p4_vtable_cross_unit` — 跨单元 trait impl + 稳定工厂符号

`iface/jbif/jbif.h.ct` 发布 `pub trait Backend`、`pub struct Handle { Backend js; }` 与
`pub Backend backend_create(void);`；`jb_null/backend_null.ct` 不写 namespace，
`import jbif::jbif;` 后在文件里定义 `Backend jbif_backend_create(void)`（落码
`struct jbif_Backend_iface jbif_backend_create(void)`），返回堆对象装箱。

```sh
cd p4_vtable_cross_unit
mkdir -p out
ct build --emit-c --ns-root iface -I iface iface/jbif/jbif.h.ct -o out
ct build --emit-c --ns-root . -I iface -I out jb_null/backend_null.ct -o out
ct build --emit-c --ns-root . -I iface -I out app/app.ct -o out
cc -std=c99 -Wall -Wextra -Werror -I out out/app.c out/backend_null.c out/jbif.c -o bin
./bin          # turns=0 eval=-1 magic=7
```

实测：跨单元实现 trait、稳定工厂符号、接口值字段派发都成立。
**注意**：这要求实现单元 import 另一个单元（`jbif::jbif`），当前 `tools/check.sh` ⑤a
白名单不允许，采纳前必须先改 ⑤a（属方案前置，本次未改）。

## P5 `p5_inherent` — 固有方法（跨单元可用性）

`queuei/queuei.h.ct` 用 `pub struct Ring` + `impl Ring { pub static ... }` 提供
`Ring.open(...)`；`local.ct` 是同单元自用的正例。

```sh
cd p5_inherent
mkdir -p outl
ct build --emit-c local.ct -o outl
cc -std=c99 -Wall -Wextra -Werror -I outl outl/local.c -o localbin
./localbin      # depth=0
```

同单元成立（注意：`local.ct` **没有** namespace）。跨单元调用
（`import queuei::queuei;` 后写 `Ring.open(4)`）在 `ct v0.44.2` 与 `ct v0.49.0` 上都是：

```text
app/app.ct:7:19: error: use of an undeclared variable
```

**后续复核修正**：真正的触发条件是「单元声明了 `namespace`」，不是「跨单元」——
带 namespace 时**同单元**也报同一句错；去掉 namespace 后同单元与跨单元都能过。
完整复现、正/负对照与边界矩阵已单列为
[CT-DEF-001](../../docs/reviews/ct-defect-namespaced-inherent-static.md)，
最小工程在 [issue_inherent_cross_unit/](issue_inherent_cross_unit/)。

另外 `impl` 内的方法必须带函数体，只写原型报 `unexpected token`
（`pub void * open(int32_t capacity);`）。结论：命名空间单元不能用静态固有方法做构造入口，
队列的 SHIM 不能换成 `Ring.open(...)`。

---

## 复现注意

- 所有 `out*/`、可执行文件、`.ctcache/` 都是生成物，已清理；按上面的命令可重建。
- `ct build --emit-c` 只生成 C 并打印建议 `cc` 行，不代跑链接；`a.out` 需自己按打印行编译。
- 探针只依赖 CT 本身，不依赖 QuickJS，也不依赖本仓库的 `tools/check.sh`。
