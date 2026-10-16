param([switch]$Test)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '../desktop')
try {
    if ($Test) { cargo test --locked; if ($LASTEXITCODE) { throw 'Desktop tests failed' } }
    cargo build --release --locked
    if ($LASTEXITCODE) { throw 'Desktop build failed' }
} finally { Pop-Location }
