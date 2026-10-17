# flash_tamaweb.ps1 -- install the tamaweb_pet firmware over ESP-Iris Vibe Mode.
#
# Two host-specific fixes carried over from build_tamaweb.ps1:
#   1) Git Bash leaks MSYSTEM into child processes; ESP-IDF's export aborts with
#      "MSys/Mingw is not supported", so scrub those vars before building.
#   2) system-update rebuilds the bundle every run and hits the ninja
#      .ninja_deps /D:/ path bug (FindFirstFileExA), so delete it first.
# The board is currently in Vibe Mode (VID_303A PID_4002, serial "ESP-IRIS");
# system-update manages the Vibe Mode transition and compatibility checks.
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.x decodes a BOM-less
# .ps1 as the system ANSI codepage (GBK here); non-ASCII comment bytes can
# swallow a line break and break parsing.
param(
    [int]$TimeoutSec = 1500,
    # Reuse build/<proj>-system-update.irisfw instead of rebuilding: use it after
    # a build that already succeeded, or when a write attempt lost its Gateway
    # lease mid-flight (the idle rebuild is what starves the lease).
    [switch]$ReuseBundle
)

$ErrorActionPreference = "Stop"

foreach ($v in @('MSYSTEM', 'MSYS', 'MINGW_PREFIX', 'MINGW_CHOST', 'CHERE_INVOKING')) {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}

$workspace = "D:\esp-mo\esp-mosaico-vibe-main"
$project = "projects/tamaweb_pet"
$deps = "$workspace\projects\tamaweb_pet\build\.ninja_deps"

if (-not $ReuseBundle) {
    if (Test-Path $deps) {
        Write-Host "[flash] removing stale .ninja_deps (ninja /D:/ path bug)"
        Remove-Item $deps -Force
    }
}

$extra = @()
if ($ReuseBundle) { $extra = @('--skip-build') }

Write-Host "[flash] iris system-update --project $project $extra (timeout $TimeoutSec s)"
Push-Location $workspace
try {
    $env:IDF_PATH = "D:\esp-mo\esp-idf"
    python mosaico.py iris system-update --project $project --timeout $TimeoutSec @extra
    if ($LASTEXITCODE -ne 0) { throw "system-update failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "[flash] OK"
