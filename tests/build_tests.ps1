# SPDX-License-Identifier: Apache-2.0
# Host unit tests for the pure-C pet model and the four minigame engines
# (no ESP-IDF, no LVGL, no engine). Kept in the repo so the -Werror compile
# lines do not rot between sessions.
# Usage: powershell -ExecutionPolicy Bypass -File projects/tamaweb_pet/tests/build_tests.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot           # projects/tamaweb_pet
$main = Join-Path $root 'main'
$gcc = 'D:\esp-mo\mingw\w64devkit\bin\gcc.exe'
if (-not (Test-Path $gcc)) { $gcc = 'gcc' }

$suites = @(
    @{ name = 'twp_model'; srcs = @('twp_model.c', 'twp_data_tables.c') },
    @{ name = 'twp_games'; srcs = @('twp_games.c', 'twp_data_tables.c') }
)

$fail = 0
foreach ($suite in $suites) {
    $test = Join-Path $PSScriptRoot ('test_' + $suite.name + '.c')
    $out = Join-Path $PSScriptRoot ('test_' + $suite.name + '.exe')
    $args2 = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-I', $main, $test)
    foreach ($s in $suite.srcs) { $args2 += (Join-Path $main $s) }
    $args2 += @('-o', $out)
    Write-Host "== compile test_$($suite.name) =="
    & $gcc @args2
    if ($LASTEXITCODE -ne 0) { Write-Host "COMPILE FAILED ($LASTEXITCODE)"; $fail = 1; continue }
    & $out
    if ($LASTEXITCODE -ne 0) { $fail = 1 }
}
Write-Host "suites exit: $fail"
exit $fail
