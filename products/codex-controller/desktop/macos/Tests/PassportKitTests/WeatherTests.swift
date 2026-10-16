import Foundation
import PassportKit
// Command Line Tools ships SwiftUI but not XCTest. Execute native model checks
// without requiring a full Xcode installation.
func check(_ condition:@autoclosure ()->Bool,_ message:String) throws {
    if !condition(){throw NSError(domain:"PassportChecks",code:1,userInfo:[NSLocalizedDescriptionKey:message])}
}
final class FailureProtocol: URLProtocol {
    override class func canInit(with request:URLRequest)->Bool {true}
    override class func canonicalRequest(for request:URLRequest)->URLRequest {request}
    override func startLoading() {
        let response=HTTPURLResponse(url:request.url!,statusCode:503,httpVersion:nil,headerFields:nil)!
        client?.urlProtocol(self,didReceive:response,cacheStoragePolicy:.notAllowed)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}
@main struct PassportChecks {
    @MainActor static func main() async throws {
        let u=WeatherAPI.searchURL("杭州 & test")
        let values=URLComponents(url:u,resolvingAgainstBaseURL:false)!.queryItems!
        try check(values.first{$0.name=="name"}?.value=="杭州 & test","City query escaping")
        let empty=try WeatherAPI.cities(data:Data("{}".utf8));try check(empty.isEmpty,"Empty city results")
        let results=try WeatherAPI.cities(data:Data(#"{"results":[{"id":1,"name":"杭州","latitude":30.2,"longitude":120.1}]}"#.utf8))
        try check(results[0].city.name=="杭州","City decoding")
        let city=City(name:"杭州",latitude:30.2,longitude:120.1)
        let data=Data(#"{"current":{"temperature_2m":-0.3,"weather_code":3,"time":1800000000}}"#.utf8)
        let weather=try WeatherAPI.weather(data:data,city:city)
        try check(weather.temperature == -3 && weather.city=="杭州","Negative temperature and city binding")
        var rejected=false
        do {_=try WeatherAPI.weather(data:Data(#"{"current":{"temperature_2m":100,"weather_code":0,"time":1800000000}}"#.utf8),city:city)} catch {rejected=true}
        try check(rejected,"Invalid weather must be rejected")
        try check(WeatherAPI.weatherURL(city).absoluteString.contains("timeformat=unixtime"),"Weather time uses UTC epoch")
        let configuration=URLSessionConfiguration.ephemeral;configuration.protocolClasses=[FailureProtocol.self]
        let session=URLSession(configuration:configuration)
        var failed=false
        do {_=try await WeatherAPI.data(from:WeatherAPI.weatherURL(city),session:session)} catch {failed=true}
        session.invalidateAndCancel();try check(failed,"HTTP failures must not be treated as current weather")
        for (phase,label) in [("Waiting","等待设备"),("Connecting","正在重连"),("Syncing","正在同步"),("BluetoothOff","蓝牙已关闭")] {
            let json="{\"phase\":\"\(phase)\",\"target\":\"p\",\"retry_enabled\":true,\"retry_count\":3,\"next_retry_at\":2000000000}"
            let connection=try JSONDecoder().decode(Connection.self,from:Data(json.utf8))
            try check(connection.retry_enabled && connection.canRetry,"Retry controls available for \(phase)")
            try check(connection.retry_count==3 && connection.retryDetail != nil,"Retry schedule decoded")
            // Labels are shared by the main window and menu bar via Snapshot.
            try check(connection.phaseLabel==label,"Native recovery label")
        }
        let model=PassportModel(preview:true)
        try check(model.snapshot==nil && model.draft.city==nil,"Preview starts no radio and invents no city")
        var closed=false;model.shutdown{closed=true};try check(closed,"Shutdown completes without a handle")
        print("Swift native checks: PASS (city search, weather decoding/bounds, recovery controls/status, preview lifecycle)")
    }
}
