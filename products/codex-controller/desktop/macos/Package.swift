// swift-tools-version: 5.9
import PackageDescription
import Foundation
let rustLibrary = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../target/macos13/release").standardized.path
let package = Package(
    name: "Passport", platforms: [.macOS(.v13)],
    products: [.executable(name: "Passport", targets: ["Passport"])],
    targets: [
        .systemLibrary(name: "CPassport", path: "Sources/CPassport"),
        .target(name: "PassportKit", dependencies: ["CPassport"], linkerSettings: [
            .unsafeFlags(["-L" + rustLibrary]), .linkedLibrary("passport_core"),
            .linkedFramework("CoreBluetooth"), .linkedFramework("CoreAudio"),
            .linkedFramework("AudioToolbox"), .linkedFramework("CoreFoundation"),
            .linkedFramework("Foundation"), .linkedFramework("IOKit"), .linkedLibrary("c++")]),
        .executableTarget(name: "Passport", dependencies: ["PassportKit"]),
        .executableTarget(name: "PassportChecks", dependencies: ["PassportKit"], path: "Tests/PassportKitTests")
    ])
