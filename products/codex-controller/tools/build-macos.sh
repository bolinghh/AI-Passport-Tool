#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../desktop"
export CMAKE_POLICY_VERSION_MINIMUM="${CMAKE_POLICY_VERSION_MINIMUM:-3.5}"
export MACOSX_DEPLOYMENT_TARGET=13.0
cargo test --locked --target-dir target/macos13
cargo build --release --locked --target-dir target/macos13
(
    cd macos
    swift run PassportChecks
    swift build -c release
)
app="target/release/Passport.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp macos/.build/release/Passport "$app/Contents/MacOS/Passport"
cp target/macos13/release/passport-codex-bridge "$app/Contents/MacOS/PassportDiagnostics"
cp ../tools/Info.plist "$app/Contents/Info.plist"
codesign --force --sign - "$app"
echo "Built $app (local ad-hoc signature; not a notarized release)."
