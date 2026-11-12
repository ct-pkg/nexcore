# 验收清单：ct-NexCore 端到端闭环（S1–S5）

- 执行日期：2026-11-07
- 最终 `./tools/check.sh`：`FINAL_RC=0`，`ALL CHECKS PASSED`
- 说明：A5 的 `NX_E_CLOSE_TIMEOUT` 分支按 spec 原文「需 S6 后可测」，本轮只验收 shutdown 后的状态语义。

- [x] A1 `send` → `drain` 投递 ≥1 且 sink 内容正确
  - `tests/drain_e2e.c`：`drain(sink) rc=0 count=2 sink.count=2 [0]={"id":1,"method":"a"} [1]={"id":2,"method":"b"}`
- [x] A2 `bootstrap_js` 被真正读取执行
  - `tests/bootstrap.c`：bootstrap `globalThis.__ok = 42;`，消息 `__ok` → `drain rc=0 count=1 last=42`
- [x] A3 QuickJS 符号进入最终二进制
  - `nm build/bin/smoke | grep -E ' T _?JS_Eval$'` → `0000000100023090 T _JS_Eval`
  - `nm build/bin/smoke | grep -cE ' _JS_[A-Za-z]'` → `289`
  - `otool -L` 仍只有 `libSystem`（QuickJS 静态链接）
- [x] A4 未注册 sink 时 drain 仍消费、不崩溃、深度归零
  - `drain(no sink) rc=0 count=0`，随后 `INBOX_DEPTH=0`、`OUTBOX_DEPTH=0`
- [x] A5 shutdown 语义
  - 关闭时排空并投递未消费消息（`drain_e2e` 第 4 段：shutdown 后 `sink.count=3`）
  - 二次 shutdown 幂等 `NX_OK`；shutdown 后 `send`/`drain` → `NX_E_SHUTDOWN`
  - `NX_E_CLOSE_TIMEOUT` 无可达分支（同步阶段无 worker 线程），留待 S6
- [x] A6 check.sh 新增 ⑦⑧⑨，负例均真实失败后恢复
  - ⑦ 负例（pop 改 LIFO）→ `NEG7_RC=6`，在 ⑦ 处中断
  - ⑧ 负例（跳过 bootstrap eval）→ `NEG8_RC=4`，`bootstrap drain count=0`
  - ⑨ 负例（pump 只处理一条）→ `NEG9_RC=9`，`count=1`（⑧ 仍为 PASS，证明是 ⑨ 自身断言生效）
  - 恢复后全量 `FINAL_RC=0`
