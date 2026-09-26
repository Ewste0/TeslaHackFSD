param(
    [string]$QtRoot = "C:\Qt\6.11.0\mingw_64",
    [string]$MinGwRoot = "C:\Qt\Tools\mingw1310_64",
    [string]$MsysRoot = "C:\msys64\mingw64"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build-release"
$distDir = Join-Path $repoRoot "dist"
$ninja = "C:\Qt\Tools\Ninja\ninja.exe"
$compiler = Join-Path $MinGwRoot "bin\g++.exe"
$deployTool = Join-Path $QtRoot "bin\windeployqt.exe"

foreach ($required in @($ninja, $compiler, $deployTool)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required tool not found: $required"
    }
}

# MinGW helper executables (cc1plus, linker) need their runtime DLLs on PATH.
$env:Path = "$(Join-Path $MinGwRoot 'bin');$(Join-Path $MsysRoot 'bin');$(Join-Path $QtRoot 'bin');$env:Path"

cmake -S $repoRoot -B $buildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$QtRoot" `
    "-DCMAKE_CXX_COMPILER=$compiler" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DMSYS2_MINGW_ROOT=$MsysRoot"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}

cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDir "TigardMapsInstaller.exe") -Destination $distDir -Force
& $deployTool --release --compiler-runtime --dir $distDir (Join-Path $distDir "TigardMapsInstaller.exe")

Write-Host "Portable build created at $distDir"
