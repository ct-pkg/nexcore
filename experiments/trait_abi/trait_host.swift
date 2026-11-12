import Foundation

func show(_ name: String, _ value: Int) {
    print("swift \(name)=\(value)")
}

show("plain size", MemoryLayout<Plain>.size)
show("plain a", MemoryLayout<Plain>.offset(of: \Plain.a) ?? -1)
show("plain b", MemoryLayout<Plain>.offset(of: \Plain.b) ?? -1)

show("one size", MemoryLayout<One>.size)
show("one a", MemoryLayout<One>.offset(of: \One.a) ?? -1)
show("one b", MemoryLayout<One>.offset(of: \One.b) ?? -1)
show("one vptr", MemoryLayout<One>.offset(of: \One.__ping_vptr) ?? -1)

show("two size", MemoryLayout<Two>.size)
show("two a", MemoryLayout<Two>.offset(of: \Two.a) ?? -1)
show("two b", MemoryLayout<Two>.offset(of: \Two.b) ?? -1)
show("two ping", MemoryLayout<Two>.offset(of: \Two.__ping_vptr) ?? -1)
show("two pong", MemoryLayout<Two>.offset(of: \Two.__pong_vptr) ?? -1)

show("holder size", MemoryLayout<Holder>.size)
show("holder tag", MemoryLayout<Holder>.offset(of: \Holder.tag) ?? -1)
show("holder inner", MemoryLayout<Holder>.offset(of: \Holder.inner) ?? -1)
show("holder tail", MemoryLayout<Holder>.offset(of: \Holder.tail) ?? -1)

show("ptrholder size", MemoryLayout<PtrHolder>.size)
show("ptrholder tag", MemoryLayout<PtrHolder>.offset(of: \PtrHolder.tag) ?? -1)
show("ptrholder inner", MemoryLayout<PtrHolder>.offset(of: \PtrHolder.inner) ?? -1)
show("ptrholder tail", MemoryLayout<PtrHolder>.offset(of: \PtrHolder.tail) ?? -1)
