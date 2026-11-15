# K-3 评审：是否为 JS 异常新增 ABI 错误码

> 结论：**不新增**。登记为 PH-12 T2，评审日期 2026-11-15。
> 上游：PH-11 卡 §10 K-3、[nexcore-ph11-stage1-battle.md](file:///Users/samxx/trae-ws/nexcore/docs/reviews/nexcore-ph11-stage1-battle.md) §3.2。

## 1. 问题

现在 eval 失败时：

- ABI 侧只置 `LAST_ERROR = NX_E_INTERNAL`（PH-11 实现，见 `src/NexCore.ct` 的 `pump_inbox`）；
- 出站侧投一条 `{"code":"E_JS_EXCEPTION","message":"<引擎文案>"}` 到 sink。

评审的问题：要不要在 ABI 错误域里加 `NX_E_JS_EXCEPTION`，让宿主能按错误类别分流？

## 2. 选项

| 选项 | 内容 | 代价 |
| --- | --- | --- |
| A 加 ABI 码 | `NexCoreError` 增 `NX_E_JS_EXCEPTION`，宿主读 `LAST_ERROR` 即可分类 | 跨端契约：Swift/Kotlin 的枚举、文档、错误映射要同步；`abi-layout.txt` 变更，⑪ 基线移动 |
| B 维持现状 | ABI 只给「成功/失败」，分类靠 `$error.code` 字符串 | 宿主若要分类得解析 JSON；ABI 面零变更 |
| C 加结构化出参 | 新增 `detail`（行号/栈）字段或函数 | 撞 I-1（7 符号）或再加一个尾字段，收益更远期 |

## 3. 判据

| 判据 | 结论 |
| --- | --- |
| 阶段 2 的宿主是否需要分类？ | **不需要**：`nex` 只区分 exit 0/1，错误统一进 stderr |
| 分类信息是否已经可得？ | **是**：`$error.code` 字符串已在协议里，`E_JS_EXCEPTION` 是既有码 |
| 加码是否有跨端成本？ | **有**：ABI 是移动端契约，加枚举值必须同步 Swift/Kotlin 与文档，超出宿主 phase 的范围 |
| 不加会不会丢信息？ | **不会**：文案在 `message`，类别在 `code`；只缺行号/栈，那属于 C 选项 |

## 4. 裁定

选 **B（维持现状）**。

- 判据：`grep -c NX_E_JS_EXCEPTION abi/abi-layout.txt` 必须为 **0**；⑪ layout golden 不移动。
- 不支持 A 的理由：分类需求尚未出现，而 ABI 变更一旦发布就要长期兼容；在宿主 phase 里顺手加属于「为假想需求设计」。
- 不支持 C 的理由：撞 7 符号冻结，且行号/栈要引擎侧改造，属于新一轮评审。

## 5. 复活条件

出现下列任一情况时重开评审（不以「更方便」为由）：

1. 移动端需要按错误类别走不同审计/重试策略；
2. 需要行号或调用栈（`detail`），且宿主无法从 `message` 稳定解析；
3. 协议侧把 `$error` 从通知改成响应，需要 ABI 同步表达。

## 6. 影响

- K-3 关闭（结论=不加）。
- PH-11 卡 §10 K-3 的「PH-12 候选」状态改为「已评审：不加」，不改原文，按 §10 的挂账纪律保留记录。
- 无代码变更，无门禁变更。
