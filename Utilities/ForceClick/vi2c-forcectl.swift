// vi2c-forcectl: read / change VoodooI2CNativeEngine Force Click settings at runtime.
//   vi2c-forcectl get
//   sudo vi2c-forcectl set ForceClickMode=1 ForceClickThreshold=140 ...
//   vi2c-forcectl config           (current configuration as KEY=VALUE, to restore it later with set)
//   vi2c-forcectl watch            (max pressure of each press, needs ForceClickDiagnostics=1; Ctrl+C stops)
import Foundation
import IOKit

// settable configuration keys (see Documentation/Force Click.md)
let configKeys = ["ForceClickMode", "ForceClickThreshold", "ForceClickHysteresis", "ForceClickDwellMs",
                  "ForceClickFollowSystem", "ForceClickFirmnessStep", "ForceClickDiagnostics"]
// read-only status keys
let statusKeys = ["ForceClickSystemFirmness", "ForceClickEffectiveThreshold", "ForceClickPressureSeen",
                  "ForceClickPressCount", "ForceClickLastPressMax", "ForceClickLastPressForced"]
let keys = configKeys + statusKeys

func engine() -> io_service_t {
    let s = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("VoodooI2CNativeEngine"))
    if s == 0 { FileHandle.standardError.write("VoodooI2CNativeEngine not found\n".data(using: .utf8)!); exit(1) }
    return s
}

func prop(_ s: io_service_t, _ k: String) -> Any? {
    IORegistryEntryCreateCFProperty(s, k as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue()
}

func show(_ s: io_service_t) {
    for k in keys { print("\(k) = \(prop(s, k).map { "\($0)" } ?? "-")") }
}

let args = Array(CommandLine.arguments.dropFirst())
let s = engine()
switch args.first ?? "get" {
case "get":
    show(s)
case "set":
    var d: [String: Int] = [:]
    for a in args.dropFirst() {
        let kv = a.split(separator: "=", maxSplits: 1).map(String.init)
        guard kv.count == 2, configKeys.contains(kv[0]), let v = Int(kv[1]) else { print("bad argument: \(a)"); exit(2) }
        d[kv[0]] = v
    }
    let r = IORegistryEntrySetCFProperties(s, d as CFDictionary)
    if r != KERN_SUCCESS {
        print(String(format: "set failed: 0x%08x%@", r, r == kIOReturnNotPrivileged ? " (run with sudo)" : ""))
        exit(3)
    }
    show(s)
case "config":
    print(configKeys.compactMap { k in (prop(s, k) as? Int).map { "\(k)=\($0)" } }.joined(separator: " "))
case "watch":
    var last = (prop(s, "ForceClickPressCount") as? Int) ?? 0
    print("press the trackpad; Ctrl+C to stop")
    while true {
        let n = (prop(s, "ForceClickPressCount") as? Int) ?? 0
        if n != last {
            last = n
            print("press \(n): max pressure \(prop(s, "ForceClickLastPressMax") ?? "-"), force \(prop(s, "ForceClickLastPressForced") ?? "-")")
        }
        usleep(50_000)
    }
default:
    print("usage: vi2c-forcectl get | config | set KEY=VALUE... | watch"); exit(2)
}
