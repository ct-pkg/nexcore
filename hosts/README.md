# hosts/：宿主实现目录

本目录存放**使用** libnexcore 的宿主实现，不属于库本体：

- 这里的代码只通过公开 ABI（`build/abi/NexCore.h`）使用库，不 `import` `src/` 里的单元，也不链接 `src/` 的中间产物。
- 库的边界规则同样适用于本目录：`tools/check-boundaries.sh` 默认扫描 `src`、`experiments/src` 与 `hosts`（PH-10 起）。
- 移动端宿主（Swift/Kotlin）的示例代码仍归各自的接入仓库；本目录只放仓库内自带的宿主，例如本地脚本解释器。

当前状态：目录已落位，**尚无实现**。宿主实现（`nex` 解释器）在 PH-11 出卡后才开始；PH-10 只负责把库层契约与门禁面准备好。
