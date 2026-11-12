# Tasks：`docs/` 方案评审

- [x] T1 复核 51 条验收原文，产出逐条判定表（PASS / FAIL / 条件 PASS），把 B-07、C-04、C-06、E-04、F 组、G-02、G-04、H-01、H-05 改写成可判定条件
- [x] T2 复核性能测量协议（G-05 + §10），补 4 项缺口（并发度、统计定义、是否含 JS handler、预算版本化）
- [x] T3 复核编号冲突清单，给出方案 A 的完整迁移表（GAP/GEN/SYM/FIX/ACC/D/Q + CR-R1..R9）与需同步修改的引用点
- [x] T4 复核两阶段关闭：四类在途对象的覆盖判定 + 三处关闭顺序矛盾 + free/drain TOCTOU + fatal shutdown 未定义
- [x] T5 复核 19 错误码：缺码（不可序列化结果）、E_NOT_SUBSCRIBED 冲突、result+error 处置冲突、$cancel 方向缺失
- [x] T6 复核生成器规则可检查性：标注「脚本/正则/人工」，指出 G-05/G-16/R-03 不可靠、G-10/G-14 只能人读
- [x] T7 复核 NexCoreConfig 冻结性：struct_size 严格相等、schema_hash 形状、无时钟注入、Swift/Kotlin 布局自检缺失
- [x] T8 复核 10 处交叉引用，逐处打开目标段确认，输出正确/错引表
- [x] T9 复核 Q-01..Q-20 默认值，输出三档分类，列出必须前移到 P1 出口的 5 条
- [x] T10 复跑 CT 实验（`/tmp/ctrepro3`：命名空间单元裸名/全限定/C 名三写法 + 导入类型），把命令与输出片段固定为证据
- [x] T11 撰写 `reviews/docs-design-review.md`（按 spec §4 结构，P0/P1/P2 + 方案缺陷/文档表述二分）
- [x] T12 验证：`git status --short` 仅 `reviews/`；`grep -rn '歧义' reviews` 为空；每条结论带 `docs/xxx.md §y` 锚点

## 依赖

T1–T9 可并行；T10 独立；T11 依赖 T1–T10；T12 依赖 T11。
