# 任务清单：ct-NexCore 端到端闭环（S1–S5）

依赖顺序执行；每项完成后跑 `./tools/check.sh` 并保留真实输出。编号不复用 `check.sh` 既有 ⓪/①–⑥。

---

## [x] T1（S1 / G1）队列出队 `pop`

**文件**
- `src/queue/queue.h.ct`
- `src/queue/queue.ct`
- `src/units.json`
- `src/queue/queue_map.m.ct`（生成物，`tools/nexcore_gen.py` 产出）

**改动**
1. `queue.h.ct` 增加：
   ```c
   /* 出队；成功时 *out_data 为 malloc 的新缓冲，调用方负责 free。*/
   pub int32_t pop(void *handle, char **out_data, int32_t *out_len);
   ```
2. `queue.ct` 实现 FIFO：空队列返回 `-1`（约定码 `NX_QUEUE_EMPTY`），参数空返回 `-3`；保持 push 的 `-2` 满队语义不变；出队后 `depth` 减 1。
3. `src/units.json` 的 `queue` 单元 `methods` 增 `"pop"`，顺序与头文件一致。
4. `src/NexCore.ct` 顶部增加局部宏 `#define NX_QUEUE_EMPTY -1`（与 `NX_QUEUE_FULL` 同风格，不导出）。

**验证**
- `./tools/check.sh` → `SCRIPT_RC=0`（⑤b 会校验 `units.json` 与 `.h.ct` 的 pub 函数一致）。
- 新增 `tests/queue_fifo.c`：push 3 条 → pop 顺序一致 → 第 4 次 pop 返回 `-1` → `depth==0`。

---

## [x] T2（S2 / G2）outbox 容器

**文件**
- `src/NexCore.ct`

**改动**
1. `struct NexCoreHandleImpl` 增字段 `void *outbox;`。
2. `nexcore_create` 中 `queue::Ring.open(NEXCORE_DEFAULT_MAX_PENDING)` 同时建立 outbox；任一失败走已有回滚分支（先 close 已建队列，再 free）。
3. `nexcore_free` 中先排空并释放 outbox 内残留缓冲，再 `queue::Ring.close(outbox)`。

**验证**
- `./tools/check.sh` 全绿。
- `tests/smoke.c` 增加断言：`nexcore_stat(h, NX_STAT_OUTBOX_DEPTH, &v)` 初始为 0 且返回 `NX_OK`（现有行为不变）。

---

## [x] T3（S3 / G3+G4）QuickJS 接入 js_host

**文件**
- `src/js_host/js_host.h.ct`
- `src/js_host/js_host.ct`
- `src/NexCore.ct`（调用点）
- `src/units.json`
- `tools/check.sh`（仅编译/链接参数）

**改动**
1. `js_host::Host.init(void *h, const char *bootstrap_js, int32_t bootstrap_len)`：替换现有 `int32_t init(void *q, int32_t bootstrap_len)`。
   - `JS_NewRuntime()` → `JS_NewContext(rt)`；失败返回 `NXJH_ERR`。
   - `bootstrap_js != 0 && bootstrap_len > 0` 时 `JS_Eval(ctx, bootstrap_js, bootstrap_len, "<bootstrap>", JS_EVAL_TYPE_GLOBAL)`，异常则 `JS_GetException` 打印并返回 `NXJH_ERR`。
   - 句柄内部结构持有 `JSRuntime *` / `JSContext *` / `void *queue`。
2. 新增 `pub int32_t eval(void *h, const char *src, int32_t len, char **out, int32_t *out_len);`：`JS_Eval` 后把结果 `JS_ToCStringLen` 拷贝成 malloc 缓冲返回（调用方 free）。
3. `js_host::Host.shutdown` 改为 `JS_FreeContext` + `JS_FreeRuntime`，释放句柄。
4. `src/NexCore.ct`：`js_host::Host.init(q, cfg->bootstrap_js, (int32_t)cfg->bootstrap_len)` —— 这是 `bootstrap_js` 第一次被真正解引用。
5. `src/units.json` 的 `js_host.methods` 改为 `["init", "eval", "shutdown", "turns"]`。
6. `tools/check.sh` 的 `cc` 行（②b/②c/⑥/smoke 四处）加：
   - `-I experiments/third_party/quickjs`
   - `experiments/third_party/quickjs/libquickjs.a -lm`（仅在链接目标：⑥ 与 smoke；`-fsyntax-only` 与 `-c` 不需链接）
7. `js_host.h.ct` 的 `#include` 增加 `<quickjs.h>`（注意 CT 对系统头的处理；若 CT 不接受，改为实现文件内 `#ct-raw #include <quickjs.h> #ct-endraw`，仅此一处例外，需在提交说明里注明原因）。

**验证**
- `./tools/check.sh` 全绿（②c 仍为 7/7）。
- `nm build/bin/smoke | grep -E ' T _?JS_Eval$'` 命中。
- 新增 `tests/bootstrap.c`：`cfg.bootstrap_js = "globalThis.__ok = 42;"`，`create` 后经 `js_host::Host.eval("__ok")` 断言输出 `"42"`。

---

## [x] T4（S4 / G5）drain 真闭环

**文件**
- `src/NexCore.ct`
- `tests/smoke.c`

**改动**
1. `nexcore_drain` 改为：
   ```c
   int32_t delivered = 0;
   for (;;) {
       void *msg; int32_t len;
       if (queue::Ring.pop(impl->queue, (char **)&msg, &len) != 0) break;   /* NX_QUEUE_EMPTY */
       char *resp = 0; int32_t resp_len = 0;
       if (js_host::Host.eval(impl->js, msg, len, &resp, &resp_len) == 0 && impl->sink != 0) {
           impl->sink(impl->sink_user, resp, (size_t)resp_len);
           delivered++;
       }
       free(msg); free(resp);
   }
   if (out_count != 0) *out_count = delivered;
   return NX_OK;
   ```
   `max_wait_ms` 用于「空队列时最多等待」的预算（无 worker 线程阶段实现为立即返回，参数不再被 `(void)` 丢弃）。
2. `NexCoreHandleImpl` 增 `void *js;` 保存 `js_host` 句柄；`nexcore_create` 保存，`nexcore_free` 调 `js_host::Host.shutdown`。

**验证**
- `tests/smoke.c` 的 `drain rc=0 count=0` 改为：注册 sink → send 2 条 → `drain` 返回 `count=2` → sink 回调计数 2 → `stat(INBOX_DEPTH)==0`。
- `./tools/check.sh` 全绿。

---

## [x] T5（S5 / G6）关闭序列

**文件**
- `src/NexCore.ct`
- `src/NexCore.h.ct`（仅注释；`NX_E_CLOSE_TIMEOUT` 已存在）

**改动**
1. `nexcore_shutdown`：置 `SHUT` → 排空 inbox（复用 drain 逻辑）→ `js_host::Host.shutdown` → 置 `FREED_JS` 标志。重复调用幂等返回 `NX_OK`。
2. 排空预算超时返回 `NX_E_CLOSE_TIMEOUT`（无 worker 阶段该分支不可达，代码保留并注释说明，待 S6 启用）。
3. `nexcore_free`：已 shutdown 时只释放剩余资源；未 shutdown 仍返回 `NX_E_BUSY`。

**验证**
- `tests/smoke.c`：shutdown 后 `send`/`drain` 均返回 `NX_E_SHUTDOWN`；二次 `shutdown` 返回 `NX_OK`；`free` 返回 `NX_OK`。
- `./tools/check.sh` 全绿。

---

## [x] T6 新增 check.sh 检查项（跟随 T1–T5 逐项增加）

**文件**：`tools/check.sh`、`tests/queue_fifo.c`、`tests/bootstrap.c`

**改动**
- 新编号：`⑦ queue FIFO`、`⑧ bootstrap 真执行`、`⑨ drain→sink 端到端`。
- 编号表追加，不复用旧号。
- 每个新项都要有一个负例（临时副本内破坏实现）确认 FAIL，再恢复。

**验证**
- 负例各失败一次；`SCRIPT_RC=0`；输出列表包含新增 ⑦⑧⑨。

---

## [x] T7 收尾复跑

- `./tools/check.sh` → `SCRIPT_RC=0`，贴真实输出。
- `nm` 7 个 ABI 符号 + `JS_Eval` 符号证据。
- 记录未能完成的容器验证前置条件（Linux 版 `ct`、可用 podman VM），不改代码绕过。
