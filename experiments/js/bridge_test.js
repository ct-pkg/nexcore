// experiments/js/bridge_test.js
// 由 CT 侧注册的 native 函数：log / nativeAdd / nativeFail /
// nativeAsync / nativeAsyncReject / nativeDone

log("sync nativeAdd(2,3)=" + nativeAdd(2, 3));
log("sync nativeAdd(-4,10)=" + nativeAdd(-4, 10));

try {
  nativeFail();
  log("UNREACHABLE nativeFail returned");
} catch (e) {
  log("native error caught: " + e.message);
}

function jsMultiply(x) {
  return x * 7;
}

(async () => {
  try {
    const v = await nativeAsync(21);
    log("async resolved=" + v);

    const z = await nativeAsyncReject(5);
    log("UNREACHABLE reject returned " + z);
  } catch (e) {
    log("async reject caught: " + e);
  }
  nativeDone();
})();

log("script tail reached (async still pending)");
