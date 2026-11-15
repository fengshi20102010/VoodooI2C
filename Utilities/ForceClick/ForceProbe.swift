// ForceProbe: measures what macOS delivers for each physical press of the touchpad.
//   ForceProbe <condition-label> <presses-per-phase> <out.csv>
// For every physical press (counted by the driver, ForceClickPressCount) it records:
//   macOS mouseDown count, highest NSEvent.stage (1 = click, 2 = Force Click), highest NSEvent.pressure,
//   and the driver's raw max TipPressure. Phases: "light" (normal click) then "hard" (press hard).
import AppKit
import IOKit

let argv = CommandLine.arguments
let label = argv.count > 1 ? argv[1] : "unnamed"
let perPhase = argv.count > 2 ? Int(argv[2]) ?? 10 : 10
let outPath = argv.count > 3 ? argv[3] : FileManager.default.currentDirectoryPath + "/results.csv"
let phases = [("light", "正常点按（不用力），共 \(perPhase) 次"), ("hard", "用力按下去，共 \(perPhase) 次")]

let engine = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("VoodooI2CNativeEngine"))
func prop(_ k: String) -> Int {
    guard engine != 0, let v = IORegistryEntryCreateCFProperty(engine, k as CFString, kCFAllocatorDefault, 0)?
        .takeRetainedValue() else { return -1 }
    if let b = v as? Bool { return b ? 1 : 0 }
    return (v as? Int) ?? -1
}

// "ms:pressure;..." from the driver's per-press trace (2-byte LE ms + 1-byte pressure per report)
func trace() -> String {
    guard engine != 0, let d = IORegistryEntryCreateCFProperty(engine, "ForceClickLastPressTrace" as CFString,
                                                              kCFAllocatorDefault, 0)?.takeRetainedValue() as? Data
    else { return "" }
    let b = [UInt8](d)
    return stride(from: 0, to: b.count - 2, by: 3).map { "\(Int(b[$0]) | Int(b[$0 + 1]) << 8):\(b[$0 + 2])" }
        .joined(separator: ";")
}

struct Row { var phase: String; var trial: Int; var clicks: Int; var stage: Int; var pressure: Double
             var raw: Int; var forced: Int; var firmness: Int; var thr: Int; var trace: String }

final class Probe: NSView {
    var rows: [Row] = []
    var phase = 0, trial = 0
    var clicks = 0, stage = 0, pressure = 0.0
    var lastCount = prop("ForceClickPressCount")
    let text = NSTextField(labelWithString: "")
    var timer: Timer?

    override init(frame: NSRect) {
        super.init(frame: frame)
        text.font = .systemFont(ofSize: 28, weight: .medium)
        text.alignment = .center
        text.frame = bounds.insetBy(dx: 20, dy: 20)
        text.autoresizingMask = [.width, .height]
        addSubview(text)
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.02, repeats: true) { [weak self] _ in self?.poll() }
    }
    required init?(coder: NSCoder) { nil }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    func refresh() {
        let p = phases[min(phase, phases.count - 1)]
        text.stringValue = "条件：\(label)\n\n阶段 \(phase + 1)/\(phases.count)：\(p.1)\n已完成 \(trial)/\(perPhase)\n\n请在这个窗口里按，不要轻点"
    }
    override func mouseDown(with e: NSEvent) { clicks += 1 }
    override func pressureChange(with e: NSEvent) {
        stage = max(stage, e.stage)
        pressure = max(pressure, Double(e.pressure))
    }
    func poll() {
        let n = prop("ForceClickPressCount")
        guard n >= 0, n != lastCount else { return }
        lastCount = n
        guard phase < phases.count else { return }
        rows.append(Row(phase: phases[phase].0, trial: trial + 1, clicks: clicks, stage: stage, pressure: pressure,
                        raw: prop("ForceClickLastPressMax"), forced: prop("ForceClickLastPressForced"),
                        firmness: prop("ForceClickSystemFirmness"), thr: prop("ForceClickEffectiveThreshold"),
                        trace: trace()))
        clicks = 0; stage = 0; pressure = 0
        trial += 1
        if trial >= perPhase { phase += 1; trial = 0 }
        if phase >= phases.count { finish() } else { refresh() }
    }
    func finish() {
        timer?.invalidate()
        var csv = (try? String(contentsOfFile: outPath, encoding: .utf8)) ?? ""
        if csv.isEmpty { csv = "condition,phase,trial,mac_clicks,mac_max_stage,mac_max_pressure,driver_raw_max,driver_forced,system_firmness,effective_threshold,trace\n" }
        for r in rows {
            csv += "\(label),\(r.phase),\(r.trial),\(r.clicks),\(r.stage),\(String(format: "%.3f", r.pressure)),\(r.raw),\(r.forced),\(r.firmness),\(r.thr),\(r.trace)\n"
        }
        try? csv.write(toFile: outPath, atomically: true, encoding: .utf8)
        var summary = "条件 \(label) 完成\n"
        for (name, _) in phases {
            let rs = rows.filter { $0.phase == name }
            let ok1 = rs.filter { $0.clicks == 1 }.count, s2 = rs.filter { $0.stage >= 2 }.count
            let extra = rs.filter { $0.clicks > 1 }.count, none = rs.filter { $0.clicks == 0 }.count
            let raws = rs.map { $0.raw }.sorted()
            summary += "\(name): 恰好1次点击 \(ok1)/\(rs.count), 多次点击 \(extra), 无点击 \(none), 进入Force Click \(s2)/\(rs.count), 原始压力 \(raws.first ?? 0)-\(raws.last ?? 0)\n"
        }
        print(summary, terminator: "")
        text.stringValue = summary + "\n结果已写入 \(outPath)\n3 秒后自动关闭"
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { NSApp.terminate(nil) }
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 600), styleMask: [.titled, .closable],
                   backing: .buffered, defer: false)
win.title = "ForceProbe - \(label)"
let probe = Probe(frame: win.contentView!.bounds)
probe.autoresizingMask = [.width, .height]
win.contentView = probe
win.center()
win.makeKeyAndOrderFront(nil)
win.makeFirstResponder(probe)
if engine == 0 { print("VoodooI2CNativeEngine not found"); exit(1) }
if prop("ForceClickDiagnostics") != 1 { print("set ForceClickDiagnostics=1 first (the scripts do this)"); exit(1) }
app.activate(ignoringOtherApps: true)
app.run()
