// 负例夹具（NOT generated）：故意缺右括号，用于证明 ⑭ 的 kotlinc 判据真的会红。
// 判据：kotlinc 必须非零退出，且输出里出现 "error:"。
// 不要修好这个文件——它变绿说明 Kotlin 编译检查失效。
package nexcore.negative

fun brokenSyntax(value: Int): Int {
    return value + 1
