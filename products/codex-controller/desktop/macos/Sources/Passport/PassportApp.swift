import SwiftUI
import AppKit
import PassportKit

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
    static weak var model: PassportModel?
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool {false}
    func applicationShouldTerminate(_ sender:NSApplication)->NSApplication.TerminateReply {
        guard let model=Self.model else{return .terminateNow}
        model.shutdown {sender.reply(toApplicationShouldTerminate:true)}
        return .terminateLater
    }
}
@main struct PassportApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var model: PassportModel
    init() {
        let model=PassportModel(preview:CommandLine.arguments.contains("--preview"))
        _model=StateObject(wrappedValue:model);AppDelegate.model=model
    }
    var body: some Scene {
        Window("Passport",id:"main") {
            MainView().environmentObject(model).frame(minWidth:740,minHeight:540)
        }.defaultSize(width:900,height:650)
        Settings { PreferencesView().environmentObject(model).frame(width:600,height:560) }
        MenuBarExtra("Passport",systemImage:model.snapshot?.mic == true ? "mic.fill" : "rectangle.connected.to.line.below") {
            MenuView().environmentObject(model)
        }
    }
}
struct MenuView: View {
    @EnvironmentObject var model: PassportModel
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        Text(model.snapshot?.phaseLabel ?? "等待连接")
        Text(model.snapshot?.mic == true ? "麦克风正在传音" : "麦克风已关闭")
        if model.snapshot?.connection.canRetry == true {
            Button("立即重试") {model.send(["type":"retry"])}
        }
        if model.snapshot?.connection.retry_enabled == true {
            Button("停止重连") {model.send(["type":"disconnect"])}
        }
        Divider()
        Button("打开 Passport") {openWindow(id:"main");NSApp.activate(ignoringOtherApps:true)}
        Button("停止传音") {model.send(["type":"stop"])}.disabled(model.snapshot?.mic != true)
        Divider()
        Button("退出 Passport") {NSApp.terminate(nil)}.keyboardShortcut("q")
    }
}
enum Page: String, CaseIterable, Identifiable {
    case device="设备", microphone="麦克风", preferences="偏好设置"
    var id:String{rawValue}
    var symbol:String {switch self{case .device:return "rectangle.portrait";case .microphone:return "mic";case .preferences:return "gearshape"}}
}
final class NavigationModel: ObservableObject { @Published var selection: Page? = .device }
struct MainView: View {
    @EnvironmentObject var model: PassportModel
    @StateObject private var navigation = NavigationModel()
    var body: some View {
        NavigationSplitView {
            List(Page.allCases,selection:$navigation.selection){page in Label(page.rawValue,systemImage:page.symbol).tag(page)}
                .navigationSplitViewColumnWidth(min:165,ideal:185,max:220)
            .safeAreaInset(edge:.bottom) {
                HStack {Circle().fill(model.snapshot?.connected == true ? Color.green : Color.secondary).frame(width:7,height:7)
                    Text(model.snapshot?.phaseLabel ?? "等待连接").font(.caption);Spacer()}.padding()
            }
        } detail: {
            VStack(spacing:0) {
                if model.preview {Text("界面预览 · 未连接设备").font(.caption).frame(maxWidth:.infinity).padding(8).background(.yellow.opacity(0.15))}
                switch navigation.selection ?? .device {
                case .device:DeviceView()
                case .microphone:MicrophoneView()
                case .preferences:PreferencesView()
                }
                if !model.localError.isEmpty || !(model.snapshot?.error ?? "").isEmpty {
                    Divider()
                    Label(model.localError.isEmpty ? (model.snapshot?.error ?? "") : model.localError,systemImage:"exclamationmark.triangle")
                        .font(.callout).foregroundStyle(.red).textSelection(.enabled).frame(maxWidth:.infinity,alignment:.leading).padding()
                }
            }.navigationTitle((navigation.selection ?? .device).rawValue)
        }
    }
}
struct DeviceView: View {
    @EnvironmentObject var model: PassportModel
    var body: some View {
        ScrollView {
            VStack(alignment:.leading,spacing:24) {
                HStack(spacing:16) {
                    Image(systemName:"rectangle.portrait").font(.system(size:40,weight:.light)).foregroundStyle(.secondary)
                    VStack(alignment:.leading,spacing:6) {
                        Text("AI Passport").font(.title2.weight(.semibold))
                        Text(model.snapshot?.text.isEmpty == false ? model.snapshot!.text : "连接你的 Passport，开始使用桌面应用。").foregroundStyle(.secondary)
                    }
                    Spacer()
                    if model.snapshot?.busy == true {ProgressView().controlSize(.small)}
                }
                GroupBox {
                    VStack(alignment:.leading,spacing:14) {
                        HStack {Text(model.snapshot?.phaseLabel ?? "等待连接").font(.headline);Spacer()
                            Button("重新扫描"){model.send(["type":"scan"])}.disabled(model.snapshot?.stopped == true)
                            Button(model.snapshot?.connected == true ? "断开连接" : "停止重连"){model.send(["type":"disconnect"])}.disabled(model.snapshot?.connected != true && model.snapshot?.busy != true && model.snapshot?.connection.retry_enabled != true)
                        }
                        if let state=model.snapshot,state.connection.canRetry {
                            HStack {
                                if let detail=state.connection.retryDetail {Text(detail).font(.callout).foregroundStyle(.secondary)}
                                Spacer()
                                Button("立即重试") {model.send(["type":"retry"])}
                            }
                        }
                        if let state=model.snapshot,!state.devices.isEmpty {
                            ForEach(state.devices){device in HStack{Label(device.name,systemImage:"antenna.radiowaves.left.and.right");Spacer()
                                Button("连接"){model.send(["type":"connect","id":device.id])}.disabled(state.stopped || (device.id == state.connection.target && (state.busy || state.connected)))}}
                        } else {Text("打开 Passport 并放在 Mac 附近。首次连接请按系统提示完成蓝牙配对。").font(.callout).foregroundStyle(.secondary)}
                    }.padding(6)
                }
                HStack(alignment:.top,spacing:32) {
                    VStack(alignment:.leading,spacing:10) {
                        Text("设备状态").font(.headline)
                        Text("电量  \(model.snapshot?.battery.map{"\($0)%"} ?? "未知")")
                        Text("蓝牙  \(model.snapshot?.rssi.map{"\($0) dBm"} ?? "未知")")
                        Text("时间与时区跟随这台 Mac").font(.callout).foregroundStyle(.secondary)
                        Button("立即同步时间"){model.syncTime()}.disabled(model.snapshot?.connected != true)
                    }
                    Spacer()
                    VStack(alignment:.leading,spacing:10) {
                        Text("番茄钟").font(.headline)
                        Text(model.snapshot?.connected == true ? (model.snapshot!.timer.formatted) : "--:--").font(.system(size:32,weight:.medium,design:.rounded)).monospacedDigit()
                        if let state=model.snapshot,state.connected {Text("\(state.timer.rest ? "休息" : "专注") · \(state.timer.label)").foregroundStyle(.secondary)}
                        Text("在 Passport 上操作，离开页面继续计时。").font(.callout).foregroundStyle(.secondary)
                    }
                }
                Divider()
                VStack(alignment:.leading,spacing:10) {
                    Text("主页天气").font(.headline)
                    if let weather=model.draft.weather {
                        Text("\(weather.city)  \(Double(weather.temperature)/10,specifier:"%.1f") °C")
                        Text("数据时间：\(Date(timeIntervalSince1970:Double(weather.fetched_at)).formatted(date:.abbreviated,time:.shortened))").font(.caption).foregroundStyle(.secondary)
                        if Date().timeIntervalSince1970-Double(weather.fetched_at)>=3600 {Text("天气已过期").foregroundStyle(.orange)}
                    } else {Text(model.draft.city == nil ? "到偏好设置选择天气城市。" : "尚未获取天气。").foregroundStyle(.secondary)}
                    HStack {Button("刷新天气"){model.refreshWeather()}.disabled(model.draft.city == nil || model.weatherLoading);if model.weatherLoading{ProgressView().controlSize(.small)}}
                    if !model.weatherError.isEmpty {Text(model.weatherError).font(.callout).foregroundStyle(.orange)}
                }
            }.padding(28).frame(maxWidth:820,alignment:.leading)
        }
    }
}
struct MicrophoneView: View {
    @EnvironmentObject var model: PassportModel
    var unavailable:Bool {!model.outputs.contains(model.draft.output)}
    var body: some View {
        Form {
            Section {
                HStack {Image(systemName:model.snapshot?.mic == true ? "mic.fill":"mic.slash").font(.title)
                    VStack(alignment:.leading,spacing:5){Text(model.snapshot?.mic == true ? "正在传音":"麦克风已关闭").font(.headline)
                        Text("在 Passport 麦克风应用中按确定键开始。离开应用即停止。").foregroundStyle(.secondary)}}.padding(.vertical,8)
                Button("停止传音"){model.send(["type":"stop"])}.disabled(model.snapshot?.mic != true)
            }
            Section("音频输出") {
                Picker("输出端点",selection:$model.draft.output) {
                    if unavailable {Text("\(model.draft.output)（不可用）").tag(model.draft.output)}
                    ForEach(model.outputs,id:\.self){Text($0).tag($0)}
                }.disabled(model.snapshot?.mic == true || model.snapshot?.diagnostic == true)
                HStack {Button("刷新设备"){model.refreshOutputs()};Button("保存输出"){model.save()}.disabled(model.snapshot?.mic == true)}
                if unavailable {Label(model.preview ? "预览模式未枚举系统音频设备。" : "当前输出不可用，请安装 BlackHole 2ch 后刷新。",systemImage:"exclamationmark.triangle").foregroundStyle(.orange)}
                Text("Passport → 本程序解码 → BlackHole 2ch → 语音应用\n在目标语音应用中选择 BlackHole 2ch 作为麦克风。").font(.callout).foregroundStyle(.secondary).fixedSize(horizontal:false,vertical:true)
            }
            Section("路由诊断") {
                HStack {
                    Button("播放测试音"){model.send(["type":"tone","output":model.draft.output])}
                    Button("测试 BlackHole 录回"){model.send(["type":"loopback","output":model.draft.output])}.disabled(!model.draft.output.contains("BlackHole"))
                    if model.snapshot?.diagnostic == true {ProgressView().controlSize(.small)}
                }.disabled(unavailable || model.snapshot?.mic == true || model.snapshot?.diagnostic == true)
                ProgressView(value:Double(model.snapshot?.level ?? 0),total:100){Text("桌面解码电平 \(model.snapshot?.level ?? 0)%")}
                if let state=model.snapshot {
                    Text("收到 \(state.audio_packets) 包 · 解码 \(state.decoded_packets) 包 · 队列丢弃 \(state.dropped_packets) 包 · 错误 \(state.decode_errors) 包").font(.caption).foregroundStyle(.secondary)
                    if !state.diagnostic_result.isEmpty {Text(state.diagnostic_result).font(.callout).textSelection(.enabled)}
                }
                Text("录回测试仅使用所选虚拟输入，不采集默认麦克风，不保存录音。").font(.caption).foregroundStyle(.secondary)
            }
        }.formStyle(.grouped)
    }
}
struct PreferencesView: View {
    @EnvironmentObject var model: PassportModel
    var body: some View {
        Form {
            Section("天气城市") {
                HStack{TextField("搜索城市",text:$model.search).onSubmit{model.searchCities()};Button("搜索"){model.searchCities()}.disabled(model.search.trimmingCharacters(in:.whitespaces).isEmpty);if model.searching{ProgressView().controlSize(.small)}}
                ForEach(model.results){city in Button {model.choose(city.city)} label:{HStack{Text(city.name);Spacer();Text(city.detail).foregroundStyle(.secondary)}}.buttonStyle(.plain)}
                if model.searchFinished && model.results.isEmpty {Text("没有找到城市，请换一个名称搜索。").foregroundStyle(.secondary)}
                if model.draft.city != nil {
                    TextField("设备显示名称",text:Binding(get:{model.draft.city?.name ?? ""},set:{model.draft.city?.name=$0}))
                    Text("当前城市：\(model.draft.city!.name)").font(.callout)
                    Button("保存城市并刷新"){if model.save(){model.refreshWeather(for:model.draft.city)}}
                }
                Text("每30分钟更新。不请求定位权限；网络失败时显示缓存及数据时间。").font(.caption).foregroundStyle(.secondary)
                Link("天气数据：Open-Meteo · 非商业使用",destination:URL(string:"https://open-meteo.com/")!)
                if !model.weatherError.isEmpty {Text(model.weatherError).foregroundStyle(.orange)}
            }
            Section("番茄钟") {
                Stepper("专注 \(model.draft.focus_minutes) 分钟",value:$model.draft.focus_minutes,in:1...120)
                Stepper("休息 \(model.draft.rest_minutes) 分钟",value:$model.draft.rest_minutes,in:1...120)
                Button("保存计时设置"){model.save()}
                Text("正在运行的阶段保持原时长，新设置从下一阶段生效。").font(.caption).foregroundStyle(.secondary)
                Button("允许完成通知"){model.requestNotifications()}
            }
            Section("运行方式") {
                Text("关闭窗口后继续在菜单栏运行；退出 Passport 会停止传音并断开设备。").font(.callout).foregroundStyle(.secondary)
                Text("麦克风音频仅实时传送，不保存录音。").font(.callout).foregroundStyle(.secondary)
            }
        }.formStyle(.grouped)
    }
}
