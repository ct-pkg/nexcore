# hosts/：宿主实现目录

本目录存放**使用** libnexcore 的宿主实现，不属于库本体：

- 这里的代码只通过公开 ABI（`build/abi/NexCore.h`）使用库，不 `import` `src/` 里的单元，也不链接 `src/` 的中间产物。
- 库的边界规则同样适用于本目录：`tools/check-boundaries.sh` 默认扫描 `src`、`experiments/src` 与 `hosts`（PH-10 起）；`hosts/**/*.c|*.h` 只允许系统头与 `"NexCore.h"`。
- 移动端宿主（Swift/Kotlin）的示例代码仍归各自的接入仓库；本目录只放仓库内自带的宿主，例如本地脚本解释器。

## shell/：`nex` 解释器（PH-11）

当前实现：[`shell/main.c`](file:///Users/samxx/trae-ws/nexcore/hosts/shell/main.c)。

| 项 | 口径 |
| --- | --- |
| 输入 | `nex <script>`；脚本按**程序模式**求值（多语句、`#!` 首行由引擎处理） |
| 原生能力 | 只注册 `Shell.print`（走 `NexCoreConfig` 的 `native_call` / `native_names` 尾字段） |
| 输出 | 脚本完成值打到 stdout；`$error`（`code=E_JS_EXCEPTION`）打到 stderr；`null` 完成值不打 |
| 退出码 | `0` 成功；`1` 脚本错误（`stat(LAST_ERROR)` 非 0）；`2` 用法或读文件失败 |
| 产物 | `build/bin/nex`，**不安装**到系统目录、不作为发布物（裁定 §7.3） |

构建与验收：

```bash
NEXCORE_JS=quickjs bash tools/check.sh      # 产出 build/bin/libnexcore.a
bash tools/check-host.sh                    # 独立宿主验收（不进 check.sh 绿线）
```

运行与 shebang：

```bash
build/bin/nex script.nexjs
# 脚本首行写 #!/usr/bin/env nex 时，需把 build/bin 放进 PATH：
PATH="$PWD/build/bin:$PATH" ./script.nexjs
# 未安装时也可用绝对路径验证内核传参：
printf '#!%s\nvar b=2; b+1\n' "$PWD/build/bin/nex" > /tmp/t.nexjs && chmod +x /tmp/t.nexjs && /tmp/t.nexjs
```

范围：本阶段没有 `-e`、`process.argv/env`、stdin、SIGINT、`Shell.spawn`（阶段 2 起，见 PH-11 卡 §7）。
