param([string]$DevkitPro = 'C:\devkitPro')
$ErrorActionPreference = 'Stop'
$buildDirectory = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $buildDirectory | Out-Null
$compiler = Join-Path $DevkitPro 'devkitA64\bin\aarch64-none-elf-gcc.exe'
$archiver = Join-Path $DevkitPro 'devkitA64\bin\aarch64-none-elf-ar.exe'
$sourceDirectory = Join-Path $PSScriptRoot 'source'
$flags = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-march=armv8-a+crc+crypto',
           '-mtune=cortex-a57', '-mtp=soft', '-fPIE', '-D__SWITCH__',
           "-I$DevkitPro\libnx\include", "-I$sourceDirectory")
foreach ($name in @('screen_overlay', 'platform_switch')) {
    & $compiler @flags -c (Join-Path $sourceDirectory "$name.c") -o (Join-Path $buildDirectory "$name.o")
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $name" }
}
& $archiver rcs (Join-Path $buildDirectory 'libmzm-switch-platform.a') (Join-Path $buildDirectory 'screen_overlay.o') (Join-Path $buildDirectory 'platform_switch.o')
if ($LASTEXITCODE -ne 0) { throw 'Library creation failed' }
# This library is a platform backend, not a playable .nro.
Write-Output "Built $buildDirectory\libmzm-switch-platform.a"
