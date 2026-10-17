# build_tamaweb.ps1 —— tamaweb_pet 固件构建脚本（沿用 weiqi-quest 固化工作流）。
#
# 背景：本机 ninja 的 .ninja_deps 会记录 GSP 生成文件的 /D:/ 风格路径，
# 增量构建时 FindFirstFileExA 报错（ninja: error ... syntax is incorrect）。
# 修复：每次构建前无条件删除 .ninja_deps，因此每次都是全量重编。
# 想保住增量，先自检再决定动不动索引：
#   python -c "print(open('build/.ninja_deps','rb').read().count(b'/D:/'))"
# 非 0 才移走 .ninja_deps（用 Move-Item），绝不移 .ninja_log。
param(
    [switch]$Clean   # 额外清理整个 build 目录（配置也重跑）
)

$ErrorActionPreference = "Stop"

# ESP-IDF's export scripts abort with "MSys/Mingw is not supported" when these
# are set. This build is often launched from a Git Bash shell that leaks them
# into the child PowerShell, so scrub them for the duration of the build.
foreach ($v in @('MSYSTEM', 'MSYSCON', 'MINGW_PREFIX', 'MINGW_CHOST', 'CHERE_INVOKING')) {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}

$workspace = "D:\esp-mo\esp-mosaico-vibe-main"
$project = "projects/tamaweb_pet"
$build = "$workspace\projects\tamaweb_pet\build"
$idf = "D:\esp-mo\esp-idf"
$script = "$workspace\submodule\esp-mosaico-utils\mosaico-tools\skills\idf-low-noise-build\scripts\idf_low_noise_build.py"

if ($Clean -and (Test-Path $build)) {
    Write-Host "[build] clean: removing $build"
    Remove-Item $build -Recurse -Force
}

if (Test-Path "$build\.ninja_deps") {
    Write-Host "[build] removing stale .ninja_deps (ninja /D:/ path bug)"
    Remove-Item "$build\.ninja_deps" -Force
}

Write-Host "[build] building $project ..."
Push-Location $workspace
try {
    $env:IDF_PATH = $idf
    python $script --project $project build
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "[build] OK"
