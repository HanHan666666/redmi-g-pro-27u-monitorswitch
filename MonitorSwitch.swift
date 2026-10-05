// MonitorSwitch — Redmi G Pro 27U 信号源切换菜单栏工具
// 原理与命令提取自 YiHoooong/Mimonitor_Toolbox（MIT）：
//   切源: am start -a com.xiaomi.mitv.tvplayer.EXTSRC_PLAY
//         -n com.xiaomi.mitv.tvplayer/.ExternalSourceActivity --ei input <ID> -f 0x10000000
//   读源: settings get global mitv.tvplayer.hdmi.last.source
//   ID:   23=HDMI1 24=HDMI2 29=DP 30=USB-C
import SwiftUI
import AppKit
import os

// MARK: - 常量

struct SourceItem: Identifiable {
    let id: Int
    let name: String
    static let all: [SourceItem] = [
        SourceItem(id: 29, name: "DP"),
        SourceItem(id: 23, name: "HDMI 1"),
        SourceItem(id: 24, name: "HDMI 2"),
        SourceItem(id: 30, name: "USB-C"),
    ]
}

// MARK: - 配置（全部存 UserDefaults）

enum Config {
    private static let key = "MonitorSwitch.deviceAddress"

    static var deviceAddress: String? {
        get { UserDefaults.standard.string(forKey: key) }
        set {
            if let newValue {
                UserDefaults.standard.set(newValue, forKey: key)
            } else {
                UserDefaults.standard.removeObject(forKey: key)
            }
        }
    }

    /// 允许输入裸 IP，自动补 :5555
    static func normalize(_ raw: String) -> String? {
        let addr = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !addr.isEmpty, addr.contains(".") else { return nil }
        return addr.contains(":") ? addr : addr + ":5555"
    }
}

// MARK: - ADB

/// 管道数据收集器：readabilityHandler 多线程回调安全累积
private final class PipeCollector {
    private let lock = NSLock()
    private var data = Data()

    func append(_ chunk: Data) {
        lock.lock()
        data.append(chunk)
        lock.unlock()
    }

    var text: String {
        lock.lock()
        defer { lock.unlock() }
        return String(data: data, encoding: .utf8) ?? ""
    }
}

final class Adb {
    private static var cachedBinary: String?

    static func binary() -> String? {
        if let cached = cachedBinary { return cached }
        let candidates = [
            "/opt/homebrew/bin/adb",
            "/usr/local/bin/adb",
            NSHomeDirectory() + "/Library/Android/sdk/platform-tools/adb",
        ]
        for path in candidates where FileManager.default.isExecutableFile(atPath: path) {
            cachedBinary = path
            return path
        }
        let which = run("/bin/zsh", ["-lc", "command -v adb"])
        if which.code == 0,
           !which.out.isEmpty,
           FileManager.default.isExecutableFile(atPath: which.out) {
            cachedBinary = which.out
            return which.out
        }
        return nil
    }

    /// 所有设备 I/O 串行执行：长指令（音量连发可达数秒）不会与轮询/其他写入交错
    private static let ioQueue = DispatchQueue(label: "monitorswitch.adb.io", qos: .userInitiated)

    private static func run(_ path: String, _ args: [String], timeout: TimeInterval = 3) -> (code: Int32, out: String) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: path)
        process.arguments = args
        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        // 后台持续排空两个管道：子进程写满 64KB 缓冲会阻塞在 write 上导致永久死锁
        let out = PipeCollector()
        let err = PipeCollector()
        stdoutPipe.fileHandleForReading.readabilityHandler = { handle in
            let chunk = handle.availableData
            out.append(chunk)
            if chunk.isEmpty { handle.readabilityHandler = nil }
        }
        stderrPipe.fileHandleForReading.readabilityHandler = { handle in
            let chunk = handle.availableData
            err.append(chunk)
            if chunk.isEmpty { handle.readabilityHandler = nil }
        }

        do {
            try process.run()
        } catch {
            return (-1, "")
        }
        // 超时强杀：adb 碰上网络抖动可能无限挂起，绝不能拖死调用线程
        let deadline = Date().addingTimeInterval(timeout)
        while process.isRunning && Date() < deadline {
            Thread.sleep(forTimeInterval: 0.05)
        }
        if process.isRunning {
            process.terminate()
            Thread.sleep(forTimeInterval: 0.3)
            if process.isRunning { kill(process.processIdentifier, SIGKILL) }
            return (-1, "")
        }
        Thread.sleep(forTimeInterval: 0.05) // 留时间给 handler 刷完残余数据
        let text = out.text.trimmingCharacters(in: .whitespacesAndNewlines)
        return (process.terminationStatus, text)
    }

    /// adb -s <addr> ...；未配置地址或找不到 adb 返回 nil
    static func exec(_ args: [String], timeout: TimeInterval = 3) -> (code: Int32, out: String)? {
        guard let bin = binary(), let address = Config.deviceAddress else { return nil }
        var result: (code: Int32, out: String)?
        ioQueue.sync { result = run(bin, ["-s", address] + args, timeout: timeout) }
        return result
    }

    static func connect() -> Bool {
        guard let bin = binary(), let address = Config.deviceAddress else { return false }
        var ok = false
        ioQueue.sync {
            _ = run(bin, ["connect", address])
            ok = run(bin, ["-s", address, "get-state"]).out == "device"
        }
        return ok
    }

    static func shell(_ command: String, timeout: TimeInterval = 3) -> String? {
        exec(["shell", command], timeout: timeout)?.out
    }

    static func push(_ localPath: String, _ remotePath: String) -> Bool {
        guard let bin = binary(), let address = Config.deviceAddress else { return false }
        var code: Int32 = -1
        ioQueue.sync { code = run(bin, ["-s", address, "push", localPath, remotePath], timeout: 10).code }
        return code == 0
    }

    /// 借 TvService（系统权限）执行命令。parts 中的空格以字面量 ${IFS} 编码，
    /// 由设备端 sh -c eval 展开——这是 TvService runSystemCommand 的调用约定
    /// （提取自 Mimonitor_Toolbox adb.py，勿改成真实空格，会静默失败）。
    @discardableResult
    static func tvServiceRun(_ parts: [String]) -> String? {
        let encoded = parts.map { "\\${IFS}\($0)" }.joined()
        return shell("service call TvService 3 s16 \"sh -c eval\(encoded)\"")
    }
}

// MARK: - MTK 寄存器通道（背光等真正生效的硬件写入）

enum Mtk {
    static let cacheDir = "/data/data/mitv.service/cache"
    static let jarPath = cacheDir + "/MtkDirectTool.jar"

    /// 部署内嵌的 MtkDirectTool.jar（Mimonitor_Toolbox，MIT）：
    /// 先 push 到 /sdcard，再借 TvService 系统权限复制进 mitv.service 私有目录
    static func deployJar() -> Bool {
        guard let local = Bundle.main.url(forResource: "MtkDirectTool", withExtension: "jar")?.path,
              Adb.push(local, "/sdcard/MtkDirectTool.jar") else { return false }
        Adb.tvServiceRun(["cp", "/sdcard/MtkDirectTool.jar", jarPath])
        return true
    }

    /// 寄存器直写背光（真正让屏幕变化的一步）
    @discardableResult
    static func setBacklight(_ value: Int) -> String? {
        Adb.tvServiceRun([
            "CLASSPATH=\(jarPath)", "/system/bin/app_process", cacheDir,
            "MtkDirectTool", "set", "g_disp__disp_back_light", "\(value)", "3",
        ])
    }

    /// 经 logcat 读寄存器当前值（输出约定：RESULT: GET key = value）
    static func readBacklight() -> Int? {
        _ = Adb.shell("logcat -c")
        Adb.tvServiceRun([
            "CLASSPATH=\(jarPath)", "/system/bin/app_process", cacheDir,
            "MtkDirectTool", "get", "g_disp__disp_back_light",
        ])
        Thread.sleep(forTimeInterval: 1.0)
        let log = Adb.shell("logcat -d | grep 'GET g_disp__disp_back_light' | tail -1") ?? ""
        guard let range = log.range(of: "= ") else { return nil }
        return Int(log[range.upperBound...].trimmingCharacters(in: .whitespacesAndNewlines))
    }

    /// 刷新画质管线，让寄存器改动立即生效
    static func refreshPQ() {
        _ = Adb.shell("am broadcast -a com.xiaomi.mitv.action.PIC_MODE_CHANGED --ei picmode 7")
    }
}

// MARK: - 音量直写通道（与小爱同学同一条 AudioManager 路径，瞬时一步到位）

enum VolDirect {
    static let jarPath = Mtk.cacheDir + "/VolDirectTool.jar"

    /// 部署自研直写工具：同样走 push + TvService cp
    static func deployJar() -> Bool {
        guard let local = Bundle.main.url(forResource: "VolDirectTool", withExtension: "jar")?.path,
              Adb.push(local, "/sdcard/VolDirectTool.jar") else { return false }
        Adb.tvServiceRun(["cp", "/sdcard/VolDirectTool.jar", jarPath])
        return true
    }

    /// 直写音量（STREAM_MUSIC）；返回 nil 表示 adb 层失败
    @discardableResult
    static func set(_ value: Int) -> String? {
        Adb.tvServiceRun([
            "CLASSPATH=\(jarPath)", "/system/bin/app_process", Mtk.cacheDir,
            "VolDirectTool", "set", "3", "\(value)",
        ])
    }
}

// MARK: - 状态与动作

final class AppState: ObservableObject {
    @Published var online = false
    @Published var adbMissing = false
    @Published var configured = false
    @Published var currentSource: Int?
    @Published var volume: Double = 0
    @Published var volumeMax: Double = 100
    @Published var backlight: Double = 0

    private var lastKnownBacklight = -1
    private var backlightHoldUntil = Date.distantPast
    private(set) var isDraggingBacklight = false
    private var jarReady = false

    private var lastKnownVolume = -1 // -1 = 尚未从设备读到
    private var volumeHoldUntil = Date.distantPast
    private(set) var isDraggingVolume = false

    private let volumeLog = Logger(subsystem: "com.wuhan.monitorswitch", category: "volume")

    private var timer: Timer?

    init() {
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 10, repeats: true) { [weak self] _ in
            self?.refresh()
        }
        if Config.deviceAddress == nil {
            // 首次运行：弹设置面板
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) { [weak self] in
                self?.openSettings()
            }
        }
    }

    var currentSourceName: String {
        SourceItem.all.first(where: { $0.id == currentSource })?.name ?? "未知"
    }

    func refresh() {
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let hasAddress = Config.deviceAddress != nil
            let adbFound = Adb.binary() != nil // 首次会探测 adb 路径，不能放主线程
            guard hasAddress, adbFound else {
                DispatchQueue.main.async {
                    self?.configured = hasAddress
                    self?.adbMissing = !adbFound
                    self?.online = false
                    self?.currentSource = nil
                }
                return
            }
            let online = Adb.connect() // 内部含 get-state 校验，未连上会自动重连
            var source: Int?
            var volume: (value: Int, max: Int)?
            var backlight: Int?
            if online {
                source = Int(Adb.shell("settings get global mitv.tvplayer.hdmi.last.source") ?? "")
                volume = Self.parseVolume(Adb.shell("cmd media_session volume --stream 3 --get") ?? "")
                backlight = Int(Adb.shell("settings get global picture_backlight") ?? "")
                // 首次连上：部署寄存器/音量直写工具并读一次真实背光校准
                if self?.jarReady != true {
                    self?.jarReady = true
                    _ = Mtk.deployJar()
                    _ = VolDirect.deployJar()
                    if let real = Mtk.readBacklight() {
                        backlight = real
                    }
                }
            }
            DispatchQueue.main.async {
                self?.configured = hasAddress
                self?.adbMissing = !adbFound
                self?.online = online
                self?.currentSource = source
                if let backlight, Date() >= (self?.backlightHoldUntil ?? .distantPast) {
                    self?.backlight = Double(backlight)
                    self?.lastKnownBacklight = backlight
                }
                guard let volume else { return }
                self?.volumeMax = Double(volume.max)
                // 拖动期间及发送后短时间内不回写轮询值，避免滑块被设备端的旧值拉回
                if Date() >= (self?.volumeHoldUntil ?? .distantPast) {
                    self?.volume = Double(volume.value)
                    self?.lastKnownVolume = volume.value
                }
            }
        }
    }

    /// 解析 "[V] volume is 18 in range [0..100]"
    static func parseVolume(_ output: String) -> (value: Int, max: Int)? {
        let regex = try? NSRegularExpression(pattern: "volume is (\\d+) in range \\[0\\.\\.(\\d+)\\]")
        guard
            let match = regex?.firstMatch(in: output, range: NSRange(output.startIndex..., in: output)),
            let valueRange = Range(match.range(at: 1), in: output),
            let maxRange = Range(match.range(at: 2), in: output)
        else { return nil }
        return (Int(output[valueRange]) ?? 0, Int(output[maxRange]) ?? 100)
    }

    func switchSource(_ id: Int) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard Adb.connect() else {
                DispatchQueue.main.async { self?.online = false }
                return
            }
            // 与 Toolbox 相同的顺序：先停播放器再拉起目标源，避免停在原 Activity 上
            _ = Adb.shell("am force-stop com.xiaomi.mitv.tvplayer")
            _ = Adb.shell("am start -a com.xiaomi.mitv.tvplayer.EXTSRC_PLAY "
                + "-n com.xiaomi.mitv.tvplayer/.ExternalSourceActivity "
                + "--ei input \(id) -f 0x10000000")
            Thread.sleep(forTimeInterval: 1.0) // 等系统把 last.source 写完
            let raw = Adb.shell("settings get global mitv.tvplayer.hdmi.last.source") ?? ""
            DispatchQueue.main.async {
                self?.currentSource = Int(raw) ?? id
            }
        }
    }

    /// 拖动开始：挂起轮询回写，滑块自由拖动
    func beginVolumeDrag() {
        isDraggingVolume = true
        volumeHoldUntil = .distantFuture
    }

    /// 松手时一次性设置到目标值。
    /// 注意：这台固件上 `media_session --set` 是假成功（返回成功但音量不变），
    /// 所以用 keyevent 步进：一次 adb 调用连发 |差值| 个音量键。
    func setVolume(_ target: Int) {
        isDraggingVolume = false
        volumeHoldUntil = Date().addingTimeInterval(1.5)
        applyVolume(target)
    }

    private func applyVolume(_ target: Int) {
        guard lastKnownVolume >= 0 else { return }
        let current = lastKnownVolume
        guard target != current else { return }
        let predicted = min(max(target, 0), Int(volumeMax))
        let sign = target > current ? "+" : "-"
        lastKnownVolume = predicted
        volume = Double(predicted)
        volumeHoldUntil = Date().addingTimeInterval(1.2)
        volumeLog.info("音量 \(current, privacy: .public) → \(predicted, privacy: .public) (\(sign, privacy: .public)\(abs(target - current), privacy: .public))")

        DispatchQueue.global(qos: .userInitiated).async {
            // 首选：VolDirectTool 直写（system 身份 + AudioManager，与小爱同路，瞬时）
            VolDirect.set(predicted)
            Thread.sleep(forTimeInterval: 0.5)
            if let output = Adb.shell("cmd media_session volume --stream 3 --get"),
               let actual = AppState.parseVolume(output), actual.value == predicted {
                return // 直写成功
            }
            // 回退：keyevent 步进（固件限制 ~70ms/步）
            var actualValue = current
            if let output = Adb.shell("cmd media_session volume --stream 3 --get"),
               let actual = AppState.parseVolume(output) {
                actualValue = actual.value
            }
            let step = predicted - actualValue
            guard step != 0 else { return }
            let keycode = step > 0 ? "24" : "25" // 24=VOLUME_UP, 25=VOLUME_DOWN
            let repeats = Array(repeating: keycode, count: min(abs(step), 200)).joined(separator: " ")
            _ = Adb.exec(["shell", "input keyevent \(repeats)"], timeout: 15)
            Thread.sleep(forTimeInterval: 0.4)
            if let output = Adb.shell("cmd media_session volume --stream 3 --get"),
               let actual = AppState.parseVolume(output) {
                DispatchQueue.main.async {
                    self.lastKnownVolume = actual.value
                }
            }
        }
    }

    // MARK: 背光（寄存器直写，一步到位；与 Mimonitor_Toolbox 同款三步：寄存器→广播→记账）

    func beginBacklightDrag() {
        isDraggingBacklight = true
        backlightHoldUntil = .distantFuture
    }

    func setBacklight(_ target: Int) {
        isDraggingBacklight = false
        backlightHoldUntil = Date().addingTimeInterval(1.5)
        guard lastKnownBacklight >= 0, target != lastKnownBacklight else { return }
        let predicted = min(max(target, 1), 100) // 工具箱下限是 1：0 可能黑屏
        lastKnownBacklight = predicted
        backlight = Double(predicted)
        DispatchQueue.global(qos: .userInitiated).async {
            Mtk.setBacklight(predicted) // ① 寄存器直写，真正生效
            Mtk.refreshPQ() // ② 画质管线刷新
            _ = Adb.shell("settings put global picture_backlight \(predicted)") // ③ 记账
            _ = Adb.shell("settings put global xiaomi_picture_backlight \(predicted)")
        }
    }

    func toggleMute() {
        DispatchQueue.global(qos: .userInitiated).async {
            let command = "input keyevent KEYCODE_VOLUME_MUTE"
            if Adb.exec(["shell", command])?.code != 0, Adb.connect() {
                _ = Adb.shell(command)
            }
        }
    }

    func openSettings() {
        SettingsPanelController.shared.show(initial: Config.deviceAddress ?? "") { address in
            guard let normalized = Config.normalize(address) else { return }
            Config.deviceAddress = normalized
            self.refresh()
        }
    }
}

// MARK: - 设置面板

final class SettingsPanelController: NSObject, NSWindowDelegate {
    static let shared = SettingsPanelController()
    private var panel: NSPanel?

    func show(initial: String, onSave: @escaping (String) -> Void) {
        if let panel {
            panel.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }
        let view = SettingsView(address: initial, onSave: onSave)
        let newPanel = NSPanel(contentViewController: NSHostingController(rootView: view))
        newPanel.title = "ADB 设置"
        newPanel.styleMask = [.titled, .closable]
        newPanel.isFloatingPanel = true
        newPanel.level = .floating
        newPanel.isReleasedWhenClosed = false
        newPanel.delegate = self
        newPanel.center()
        newPanel.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        panel = newPanel
    }

    func windowWillClose(_ notification: Notification) {
        // 关窗后压回无 Dock 模式（弹窗期间系统可能把应用提升为常规应用）
        NSApplication.shared.setActivationPolicy(.accessory)
    }
}

struct SettingsView: View {
    @State var address: String
    let onSave: (String) -> Void
    @State private var invalid = false

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("显示器 ADB 地址").font(.headline)
            Text("在显示器上开启「网络 ADB 调试」后获得的 IP 地址，端口可省略（默认 5555）。")
                .font(.caption)
                .foregroundColor(.secondary)
            TextField("例如 192.168.1.100:5555", text: $address)
                .textFieldStyle(.roundedBorder)
                .frame(width: 280)
            if invalid {
                Text("地址无效，请检查后重试").font(.caption).foregroundColor(.red)
            }
            HStack {
                Spacer()
                Button("取消") {
                    NSApp.keyWindow?.close()
                }
                Button("保存") {
                    if Config.normalize(address) != nil {
                        invalid = false
                        NSApp.keyWindow?.close()
                        onSave(address)
                    } else {
                        invalid = true
                    }
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
    }
}

// MARK: - 面板（.window 样式：点按钮不收起，可连点音量）

struct PanelContent: View {
    @ObservedObject var state: AppState
    @State private var sliderValue: Double = 0
    @State private var dragging = false
    @State private var backlightSlider: Double = 0
    @State private var backlightDragging = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            headerView
            Divider()
            Text("信号源").font(.caption).foregroundColor(.secondary)
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 8) {
                ForEach(SourceItem.all) { item in
                    Button {
                        state.switchSource(item.id)
                    } label: {
                        HStack {
                            Image(systemName: state.currentSource == item.id ? "checkmark.circle.fill" : "circle")
                            Text(item.name)
                        }
                        .frame(maxWidth: .infinity)
                    }
                    .controlSize(.large)
                    .buttonStyle(.bordered)
                    .tint(state.currentSource == item.id ? .accentColor : .secondary)
                }
            }
            Text("背光").font(.caption).foregroundColor(.secondary)
            HStack(spacing: 10) {
                Image(systemName: backlightIcon)
                    .foregroundColor(.secondary)
                    .frame(width: 20)
                Slider(value: $backlightSlider, in: 1...100, step: 1) { editing in
                    backlightDragging = editing
                    if editing {
                        state.beginBacklightDrag()
                    } else {
                        state.setBacklight(Int(backlightSlider.rounded()))
                    }
                }
                .disabled(!state.online)
                .onAppear {
                    if !backlightDragging { backlightSlider = state.backlight }
                }
                .onChange(of: state.backlight) { polled in
                    if !backlightDragging { backlightSlider = polled }
                }
                Text("\(Int(backlightSlider))")
                    .font(.callout.monospacedDigit())
                    .foregroundColor(.secondary)
                    .frame(width: 30, alignment: .trailing)
                    .help("背光（0–100）")
            }
            Text("音量").font(.caption).foregroundColor(.secondary)
            HStack(spacing: 10) {
                Image(systemName: volumeIcon)
                    .foregroundColor(.secondary)
                    .frame(width: 20)
                Slider(value: $sliderValue, in: 0...max(state.volumeMax, 1), step: 1) { editing in
                    dragging = editing
                    if editing {
                        state.beginVolumeDrag()
                    } else {
                        // 松手才发送，拖动过程不触发任何调整
                        state.setVolume(Int(sliderValue.rounded()))
                    }
                }
                .disabled(!state.online)
                .onAppear {
                    // 面板每次打开都会新建视图：主动取一次当前值，
                    // 否则 onChange 在"值没变"时永远不触发，滑块停在初始 0
                    if !dragging { sliderValue = state.volume }
                }
                .onChange(of: state.volume) { polled in
                    if !dragging { sliderValue = polled } // 非拖动期间同步设备值
                }
                Text("\(Int(sliderValue))")
                    .font(.callout.monospacedDigit())
                    .foregroundColor(.secondary)
                    .frame(width: 30, alignment: .trailing)
                    .help("音量（0–100）")
                Button {
                    state.toggleMute()
                } label: {
                    Image(systemName: "speaker.slash.fill")
                }
                .buttonStyle(.borderless)
                .disabled(!state.online)
                .help("静音")
            }
            Divider()
            HStack {
                Button("ADB 设置…") { state.openSettings() }
                Button("刷新") { state.refresh() }
                Spacer()
                Button("退出") { NSApplication.shared.terminate(nil) }
            }
            .controlSize(.small)
        }
        .padding(14)
        .frame(width: 320)
    }

    @ViewBuilder
    private var headerView: some View {
        if state.adbMissing {
            Label("未找到 adb，请安装 platform-tools", systemImage: "exclamationmark.circle")
                .font(.callout)
        } else if !state.configured {
            HStack {
                Label("未配置设备地址", systemImage: "questionmark.circle")
                Spacer()
                Button("去设置") { state.openSettings() }
            }
            .font(.callout)
        } else {
            Label(
                state.online ? "已连接 \(Config.deviceAddress ?? "") · 当前: \(state.currentSourceName)"
                             : "未连接 \(Config.deviceAddress ?? "")",
                systemImage: state.online ? "circle.fill" : "circle"
            )
            .font(.callout)
            .foregroundColor(state.online ? .primary : .red)
        }
    }

    private var backlightIcon: String {
        if backlightSlider <= 0 { return "moon.fill" }
        if backlightSlider < 40 { return "sun.min.fill" }
        return "sun.max.fill"
    }

    private var volumeIcon: String {
        if sliderValue <= 0 { return "speaker.slash.fill" }
        if sliderValue < state.volumeMax * 0.4 { return "speaker.wave.1.fill" }
        return "speaker.wave.3.fill"
    }
}

// MARK: - 入口

@main
struct MonitorSwitchApp: App {
    @StateObject private var state = AppState()

    init() {
        // 锁定为附加应用（无 Dock 图标）：Info.plist 的 LSUIInterface 已声明，
        // 但弹出设置面板时 AppKit 可能临时提升为常规应用，这里显式固定
        NSApplication.shared.setActivationPolicy(.accessory)
    }

    var body: some Scene {
        MenuBarExtra {
            PanelContent(state: state)
        } label: {
            if state.adbMissing {
                Image(systemName: "exclamationmark.circle")
            } else if !state.configured {
                Image(systemName: "questionmark.circle")
            } else {
                Image(systemName: state.online ? "display" : "display.slash")
            }
        }
        .menuBarExtraStyle(.window)
    }
}
