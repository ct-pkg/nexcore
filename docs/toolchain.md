# 用指定 CT 版本跑验证（本机布局说明）

> 用途：换人 / 换机器时照着做，就能在**不覆盖系统 `ct`** 的前提下，用基线版本跑 nexcore 的检查。
> 基线本身见 `docs/README.md:19`（当前 `ct v0.44.2` / `328e5e63`）；本文只讲怎么把那个版本叫起来。

## 本机布局（只增不改）

| 路径 | 是什么 | 版本 |
| --- | --- | --- |
| `~/.local/bin/ct` | 系统原有的 `ct`，**不要覆盖** | `ct v0.44.1` |
| `~/.local/bin/ctc` | 系统原有 `ctc`，指向 `ct`，不要覆盖 | `ctc v0.44.1` |
| `~/.local/bin/ct-v0.44.2` | 从 `ct-lang` tag `v0.44.2` 源码构建出的副本 | `ct v0.44.2`（经 shim 调用） |
| `~/.local/ct/0.44.2/ct` | 符号链接 → `~/.local/bin/ct-v0.44.2` | 同上 |
| `~/.local/ct/0.44.2/ctc` | 符号链接 → `~/.local/bin/ct-v0.44.2` | 同上 |

`ct` 按 `argv[0]` 自派发，`ctc` 与 `ct` 是同一个二进制，所以两个名字都要指向同一份文件。

## 怎么跑

```bash
export PATH="$HOME/.local/ct/0.44.2:$PATH"
ct --version                                # 必须打印 ct v0.44.2

bash tools/check.sh                          # 全量；末行 ALL CHECKS PASSED，rc=0
bash tools/check.sh g0                       # 规约纳管门禁
bash experiments/build.sh                    # 地基验证；末行 == ALL CHECKS PASSED ==，rc=0
```

一次性用法（不污染当前 shell）：

```bash
PATH="$HOME/.local/ct/0.44.2:$PATH" bash tools/check.sh
```

仓库根目录的 `build/` 在本机被 macOS 封存（`rm`/`mkdir`/`xattr` 全 EPERM），全量检查会先 `rm -rf build/...`，因此**原仓库跑不了**。按以下方式在副本上跑，QuickJS 静态库用符号链接复用：

```bash
rm -rf /tmp/nxrun && mkdir -p /tmp/nxrun
tar cf - --exclude=./build . | (cd /tmp/nxrun && tar xf -)
mkdir -p /tmp/nxrun/experiments/third_party
ln -s <已有 libquickjs.a 的目录> /tmp/nxrun/experiments/third_party/quickjs
cd /tmp/nxrun && PATH="$HOME/.local/ct/0.44.2:$PATH" bash tools/check.sh
```

## 为什么不能只放 `~/.local/bin/ct-v0.44.2`

两个实测原因：

1. **调用方写死裸 `ct`**。`tools/check.sh` 直接调 `ct build ...`，不读 `CT` 环境变量（只有 `experiments/build.sh` 支持 `CT=`）。PATH 里只有 `ct-v0.44.2` 时，检查会在第一条 `ct build` 处报 `command not found`，退出码 127。
2. **自报名随 `argv[0]` 变**。同一个二进制，叫 `ct` 时输出 `ct v0.44.2`，叫 `ct-v0.44.2` 时输出 `ct-ast v0.44.2`。任何解析版本号首词的脚本都会被这个前缀骗到。

所以做法是「二进制用带版本号的名字保存 + 单独一个目录放 `ct`/`ctc` 链接」，用 PATH 前缀切换版本，系统 `ct` 保持不动。

## 已验证

| 检查 | 结果 |
| --- | --- |
| `PATH="$HOME/.local/ct/0.44.2:$PATH" ct --version` | `ct v0.44.2` |
| `~/.local/bin/ct --version` | `ct v0.44.1`（未被覆盖） |
| `bash tools/check.sh`（shim PATH） | rc=0，`ALL CHECKS PASSED` |
| `bash experiments/build.sh`（shim PATH） | rc=0，`== ALL CHECKS PASSED ==` |

## 遗留

- 系统 `ct` 仍是 v0.44.1。是否把它也换掉属于工具链变更，需单独裁定，本文不做也不建议自动做。
- 本副本是从 `ct-lang` tag `v0.44.2` 构建的；如果基线再动，按同样方式另建 `~/.local/ct/<版本>/`，不要覆盖旧目录。
