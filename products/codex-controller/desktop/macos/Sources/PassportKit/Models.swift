import Foundation
public struct City: Codable, Equatable, Identifiable, Sendable {
    public var name: String
    public var latitude: Double
    public var longitude: Double
    public var id: String { "\(latitude),\(longitude)" }
    public init(name: String, latitude: Double, longitude: Double) { self.name=name;self.latitude=latitude;self.longitude=longitude }
}
public struct Weather: Codable, Equatable, Sendable {
    public var city: String
    public var fetched_at: UInt32
    public var temperature: Int16
    public var code: UInt16
}
public struct Settings: Codable, Equatable, Sendable {
    public var output = "BlackHole 2ch"
    public var peripheral = ""
    public var focus_minutes: UInt8 = 25
    public var rest_minutes: UInt8 = 5
    public var city: City?
    public var weather: Weather?
    public init() {}
}
public struct Device: Decodable, Identifiable { public var id: String; public var name: String }
public struct Connection: Decodable {
    public var phase: String; public var target: String
    public var retry_enabled: Bool; public var retry_count: UInt32; public var next_retry_at: UInt64?
    public var retryDetail: String? {
        guard retry_enabled, let at=next_retry_at else {return nil}
        let seconds=max(0,Int64(at)-Int64(Date().timeIntervalSince1970))
        return "第 \(retry_count) 次重试 · 约 \(seconds) 秒后"
    }
    public var phaseLabel: String {
        ["Waiting":"等待设备","BluetoothOff":"蓝牙已关闭","Idle":"等待连接","Scanning":"正在扫描","Connecting":"正在重连","Syncing":"正在同步",
         "Connected":"已连接","Disconnecting":"正在断开","Failed":"连接未完成"][phase] ?? phase
    }
    public var canRetry: Bool { !target.isEmpty && phase != "Connected" }
}
public struct TimerStatus: Decodable {
    public var rest: Bool; public var state: UInt8; public var remaining: UInt32; public var completion: UInt32
    public var formatted: String { String(format: "%02d:%02d", remaining / 60, remaining % 60) }
    public var label: String { ["待开始", "计时中", "已暂停", "已完成"][min(Int(state),3)] }
}
public struct Snapshot: Decodable {
    public var connection: Connection
    public var devices: [Device]
    public var connected: Bool; public var mic: Bool; public var stopped: Bool
    public var text: String; public var error: String
    public var battery: Int?; public var rssi: Int?; public var page: UInt8
    public var level: UInt32; public var audio_packets: UInt32; public var decoded_packets: UInt32
    public var dropped_packets: UInt32; public var decode_errors: UInt32
    public var timer: TimerStatus; public var config: Settings
    public var diagnostic: Bool; public var diagnostic_result: String
    public var busy: Bool { ["Scanning","Connecting","Syncing","Disconnecting"].contains(connection.phase) }
    public var phaseLabel: String { connection.phaseLabel }
}
public struct CityResult: Decodable, Identifiable, Sendable {
    public var id: Int; public var name: String; public var latitude: Double; public var longitude: Double
    public var country: String?; public var admin1: String?
    public var city: City { City(name:name, latitude:latitude, longitude:longitude) }
    public var detail: String { [admin1,country].compactMap{$0}.joined(separator:" · ") }
}
public enum WeatherAPI {
    public static func searchURL(_ query:String) -> URL {
        var u=URLComponents(string:"https://geocoding-api.open-meteo.com/v1/search")!
        u.queryItems=[.init(name:"name",value:query),.init(name:"count",value:"8"),.init(name:"language",value:"zh"),.init(name:"format",value:"json")]
        return u.url!
    }
    public static func weatherURL(_ city:City)->URL {
        var u=URLComponents(string:"https://api.open-meteo.com/v1/forecast")!
        u.queryItems=[.init(name:"latitude",value:String(city.latitude)),.init(name:"longitude",value:String(city.longitude)),
            .init(name:"current",value:"temperature_2m,weather_code"),.init(name:"timeformat",value:"unixtime")]
        return u.url!
    }
    public static func cities(data:Data)throws->[CityResult] {
        struct Results:Decodable {var results:[CityResult]?}
        return try JSONDecoder().decode(Results.self,from:data).results ?? []
    }
    public static func weather(data:Data,city:City)throws->Weather {
        struct Response:Decodable {struct Current:Decodable {var temperature_2m:Double;var weather_code:UInt16;var time:UInt32};var current:Current}
        let c=try JSONDecoder().decode(Response.self,from:data).current
        guard c.temperature_2m.isFinite,(-100...70).contains(c.temperature_2m),c.time>0 else {throw URLError(.cannotParseResponse)}
        return Weather(city:city.name,fetched_at:c.time,temperature:Int16((c.temperature_2m*10).rounded()),code:c.weather_code)
    }
    public static func data(from url:URL,session:URLSession = .shared) async throws -> Data {
        var request=URLRequest(url:url);request.timeoutInterval=10
        let (data,response)=try await session.data(for:request)
        guard let response=response as? HTTPURLResponse,(200..<300).contains(response.statusCode) else {throw URLError(.badServerResponse)}
        return data
    }
}
