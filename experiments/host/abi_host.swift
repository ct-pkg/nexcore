import Foundation

func hostCB(_ ctx: UnsafeMutableRawPointer?, _ v: Int32) -> Int32 {
    let base = ctx!.assumingMemoryBound(to: Int32.self).pointee
    return base + v
}

print("swift add=\(nexcore_abi_add(20, 22)) version=\(nexcore_abi_version())")
print("swift pair size=\(MemoryLayout<NexcorePair>.size) right_off=\(MemoryLayout<NexcorePair>.offset(of: \NexcorePair.right) ?? -1)")

var base: Int32 = 40
let p = nexcore_pair_make(7, 2.5, &base)
print("swift make left=\(p.left) right=\(p.right) ctx_ok=\(p.ctx == UnsafeMutableRawPointer(&base))")

let applied = withUnsafeMutablePointer(to: &base) { ptr in
    nexcore_apply(UnsafeMutableRawPointer(ptr), hostCB, 2)
}
print("swift apply=\(applied)")
