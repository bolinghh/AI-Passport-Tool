import Foundation
import AppKit
import UserNotifications
import CPassport

private final class TransferredCore: @unchecked Sendable {
    private let pointer: UnsafeMutableRawPointer
    init(_ pointer:UnsafeMutableRawPointer){self.pointer=pointer}
    func close(){passport_core_destroy(pointer)}
}
@MainActor public final class PassportModel: ObservableObject {
    @Published public var snapshot: Snapshot?
    @Published public var draft = Settings()
    @Published public var outputs: [String] = []
    @Published public var localError = ""
    @Published public var search = ""
    @Published public var results: [CityResult] = []
    @Published public var searching = false
    @Published public var weatherLoading = false
    @Published public var weatherError = ""
    @Published public var searchFinished = false
    public let preview: Bool
    private var core: UnsafeMutableRawPointer?
    private var pollTimer: Timer?
    private var weatherTimer: Timer?
    private var clockTimer: Timer?
    private var observers: [NSObjectProtocol] = []
    // Opt-in local acceptance trace: connection states only, never identifiers,
    // pairing values, weather, audio samples or packet contents.
    private let connectionLog: URL? = {
        let args=CommandLine.arguments
        guard let i=args.firstIndex(of:"--connection-log"),i+1<args.count else {return nil}
        return URL(fileURLWithPath:args[i+1])
    }()
    private var loggedConnection = ""
    private func logConnection(_ state:Snapshot) {
        guard let url=connectionLog else {return}
        let visibleWindows=NSApp?.windows.filter{$0.isVisible && $0.canBecomeMain}.count ?? 0
        let key="\(state.connection.phase)/\(state.connected)/\(state.mic)/\(state.connection.retry_count)/\(visibleWindows)"
        guard key != loggedConnection else {return};loggedConnection=key
        let value:[String:Any] = ["time":Date().timeIntervalSince1970,"phase":state.connection.phase,
            "connected":state.connected,"mic":state.mic,"retry":state.connection.retry_count,"pid":ProcessInfo.processInfo.processIdentifier,"visible_windows":visibleWindows]
        guard var data=try? JSONSerialization.data(withJSONObject:value,options:.sortedKeys) else {return}
        data.append(10)
        do {
            if !FileManager.default.fileExists(atPath:url.path) {FileManager.default.createFile(atPath:url.path,contents:nil,attributes:[.posixPermissions:0o600])}
            let file=try FileHandle(forWritingTo:url);defer{try? file.close()}
            try file.seekToEnd();try file.write(contentsOf:data)
        } catch { /* A diagnostic file cannot interrupt BLE or audio. */ }
    }
    private var loaded = false
    private var previousCompletion: UInt32?
    private var wasConnected = false
    private var searchTask: Task<Void,Never>?
    private var weatherTask: Task<Void,Never>?
    private var weatherGeneration: UInt64 = 0
    public init(preview: Bool = false) {
        self.preview=preview
        if preview { return }
        core=passport_core_create()
        guard core != nil else {localError="无法启动 Passport 核心，请检查本地设置文件。";return}
        poll();refreshOutputs();syncTime()
        pollTimer=Timer.scheduledTimer(withTimeInterval:0.3,repeats:true) { [weak self] _ in Task { @MainActor in self?.poll() } }
        clockTimer=Timer.scheduledTimer(withTimeInterval:60,repeats:true) { [weak self] _ in Task { @MainActor in self?.syncTime() } }
        weatherTimer=Timer.scheduledTimer(withTimeInterval:1800,repeats:true) { [weak self] _ in Task { @MainActor in self?.refreshWeather() } }
        observers.append(NSWorkspace.shared.notificationCenter.addObserver(forName:NSWorkspace.didWakeNotification,object:nil,queue:.main) { [weak self] _ in Task { @MainActor in self?.syncTime();self?.refreshWeather() } })
        for name in [Notification.Name.NSSystemTimeZoneDidChange,Notification.Name.NSSystemClockDidChange] {
            observers.append(NotificationCenter.default.addObserver(forName:name,object:nil,queue:.main) { [weak self] _ in Task { @MainActor in self?.syncTime() } })
        }
        if draft.city != nil {refreshWeather()}
    }
    private func owned(_ pointer:UnsafeMutablePointer<CChar>?)->String? {
        guard let pointer else {return nil};defer{passport_string_free(pointer)};return String(cString:pointer)
    }
    @discardableResult public func send(_ value:[String:Any]) -> Bool {
        guard let core else { if !preview {localError="后台已停止"};return false }
        do {
            let data=try JSONSerialization.data(withJSONObject:value)
            let json=String(decoding:data,as:UTF8.self)
            let error=json.withCString{owned(passport_core_send(core,$0))} ?? "后台调用失败"
            localError=error;return error.isEmpty
        } catch {localError=error.localizedDescription;return false}
    }
    private func poll() {
        guard let core,let json=owned(passport_core_snapshot(core)) else {return}
        do {
            let state=try JSONDecoder().decode(Snapshot.self,from:Data(json.utf8))
            snapshot=state;logConnection(state)
            if !loaded {draft=state.config;loaded=true}
            if state.connected && !wasConnected {syncTime();if draft.city != nil {refreshWeather()}}
            if !state.connected {previousCompletion=nil}
            else if let old=previousCompletion,state.timer.completion>old {notifyTimer(rest:state.timer.rest)}
            if state.connected {previousCompletion=state.timer.completion}
            wasConnected=state.connected
        } catch {localError="无法读取核心状态：\(error.localizedDescription)"}
    }
    public func refreshOutputs() {if let json=owned(passport_audio_outputs()),let list=try? JSONDecoder().decode([String].self,from:Data(json.utf8)){outputs=list}}
    public func syncTime() {send(["type":"time","epoch":UInt32(Date().timeIntervalSince1970),"offset":TimeZone.current.secondsFromGMT()])}
    @discardableResult public func save() -> Bool {
        draft.peripheral=snapshot?.config.peripheral ?? draft.peripheral
        if draft.city != snapshot?.config.city {draft.weather=nil}
        do {let data=try JSONEncoder().encode(draft);let object=try JSONSerialization.jsonObject(with:data)
            return send(["type":"apply","config":object])
        } catch {localError=error.localizedDescription;return false}
    }
    public func searchCities() {
        searchTask?.cancel();results=[];searchFinished=false
        let query=search.trimmingCharacters(in:.whitespacesAndNewlines);guard !query.isEmpty else {return}
        searching=true;localError=""
        searchTask=Task { [weak self] in
            do {let data=try await WeatherAPI.data(from:WeatherAPI.searchURL(query));try Task.checkCancellation()
                guard let self else {return};self.results=try WeatherAPI.cities(data:data);self.searchFinished=true;self.searching=false
            } catch {guard !Task.isCancelled else{return};self?.localError="城市搜索失败：\(error.localizedDescription)";self?.searching=false}
        }
    }
    public func choose(_ city:City) {weatherTask?.cancel();weatherLoading=false;weatherGeneration &+= 1;draft.city=city;draft.weather=nil;results=[];if save(){refreshWeather(for:city)}}
    public func refreshWeather(for selected:City? = nil) {
        guard let city=selected ?? snapshot?.config.city else {return}
        weatherTask?.cancel();weatherGeneration &+= 1;let generation=weatherGeneration
        weatherLoading=true;weatherError=""
        weatherTask=Task { [weak self] in
            do {
                let data=try await WeatherAPI.data(from:WeatherAPI.weatherURL(city));try Task.checkCancellation()
                let weather=try WeatherAPI.weather(data:data,city:city)
                guard let self,self.weatherGeneration==generation,self.draft.city==city else{return}
                let object=try JSONSerialization.jsonObject(with:JSONEncoder().encode(weather))
                if self.send(["type":"weather","weather":object]) {self.draft.weather=weather};self.weatherLoading=false
            } catch {guard !Task.isCancelled else{return};self?.weatherLoading=false;self?.weatherError="刷新失败，保留上次天气：\(error.localizedDescription)"}
        }
    }
    public func requestNotifications() {UNUserNotificationCenter.current().requestAuthorization(options:[.alert,.sound]){_,_ in}}
    private func notifyTimer(rest:Bool) {
        let content=UNMutableNotificationContent();content.title=rest ? "休息结束" : "专注完成"
        content.body="在 Passport 确认后开始下一阶段。" // No audio during microphone streaming.
        let request=UNNotificationRequest(identifier:UUID().uuidString,content:content,trigger:nil)
        UNUserNotificationCenter.current().add(request){_ in}
    }
    public func shutdown(completion:@escaping ()->Void) {
        pollTimer?.invalidate();weatherTimer?.invalidate();clockTimer?.invalidate();searchTask?.cancel();weatherTask?.cancel()
        for observer in observers {NotificationCenter.default.removeObserver(observer);NSWorkspace.shared.notificationCenter.removeObserver(observer)}
        observers=[]
        guard let core else{completion();return};self.core=nil
        let transferred=TransferredCore(core)
        DispatchQueue.global(qos:.userInitiated).async {transferred.close();DispatchQueue.main.async{completion()}}
    }
}
