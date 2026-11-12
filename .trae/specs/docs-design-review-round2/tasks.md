# Tasks：docs v0.3 整改复核（第二轮）

- [ ] T1 固化 P0-1 多单元转发的 5 个问题：把 design §3.4、GEN-17/SYM-04、GAP-05、D-15 的证据整理成「A→B 路径 / 根单元职责 / 函数指针接口与生命周期 / include·符号·初始化顺序 / 生成器形态 + 最小产物示意」五段，附最小产物示意（标注「评审者构造，文档未给」）。
- [ ] T2 固化「未实测」诚实性：引用 /tmp/ctm5 与 /tmp/ctm6 的完整命令与输出，对比 design L113-L114 与 audit L229。
- [ ] T3 关闭顺序 4 项：design/protocol 是否只引用；四类在途对象计数；`CLOSE_BUDGET_EXCEEDED` 调用方语义；`js_done` 竞态（UAF、双 DEAD、interrupt 冲突）。
- [ ] T4 编号迁移：全库 grep 旧前缀，列出仅有历史说明的三处，并声明不算残留。
- [ ] T5 验收抽检 15 条：A-04/A-08/A-09/A-10/A-11/B-03/B-07/C-03/C-05/C-11/D-03/E-04/E-08/G-05/H-05，逐条判「可判定 / 条件可判定 / 不可判定」。
- [ ] T6 bench-v1：检查设备、预热、采样、离群值、重复、吞吐、含/不含 handler 的可复现性，给出换机可比结论。
- [ ] T7 NexCoreConfig：`struct_size` 单向兼容判据、`now_ms`/`clock_user` 的 Swift/Kotlin 表达。
- [ ] T8 写报告 `reviews/docs-design-review-round2.md`（按 spec §4 固定结构）。
- [ ] T9 校验：`grep -oE 'ACC-[A-H]-[0-9]{2}' docs/acceptance.md | sort -u | wc -l` = 57；旧前缀仅 README L52/L66、codegen L547；报告无不可判定措辞作为判定依据。
- [ ] T10 确认写盘范围：仅 `reviews/` 与 `.trae/specs/`，`docs/`、`experiments/` 无改动。
