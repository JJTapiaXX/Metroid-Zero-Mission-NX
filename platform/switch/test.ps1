param(
    [string]$DevkitPro = 'C:\devkitPro',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $DevkitPro 'devkitA64\bin\aarch64-none-elf-gcc.exe'
$buildDirectory = Join-Path $PSScriptRoot 'build'
$sourceDirectory = Join-Path $PSScriptRoot 'source'
$testsDirectory = Join-Path $PSScriptRoot 'tests'
New-Item -ItemType Directory -Force $buildDirectory | Out-Null
$flags = @('-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin',
    '-fno-stack-protector', '-nostdlib', '-static', "-I$sourceDirectory",
    '-Wl,-e,overlay_test', '-Wl,-Ttext=0x100000')
foreach ($test in @('overlay', 'input', 'audio_output', 'audio_volume', 'audio_filters', 'network_socket')) {
    $sources = @((Join-Path $sourceDirectory 'screen_overlay.c'), (Join-Path $testsDirectory "${test}_test.c"))
    $testFlags = $flags
    if ($test -eq 'input') {
        $sources += Join-Path $sourceDirectory 'platform_switch.c'
        $testFlags += "-I$testsDirectory\fakes"
    }
    if ($test -eq 'audio_output') {
        $sources = @((Join-Path $sourceDirectory 'audio_switch.c'), (Join-Path $sourceDirectory 'audio_filters.c'), (Join-Path $testsDirectory 'audio_output_test.c'))
        $testFlags += '-ffunction-sections', '-Wl,--gc-sections'
        $testFlags += "-I$testsDirectory\fakes"
    }
    if ($test -eq 'audio_volume') {
        $sources = @((Join-Path $sourceDirectory 'audio_volume.c'), (Join-Path $testsDirectory 'audio_volume_test.c'))
        $testFlags += '-ffunction-sections', '-Wl,--gc-sections'
    }
    if ($test -eq 'audio_filters') {
        $sources = @((Join-Path $sourceDirectory 'audio_filters.c'), (Join-Path $testsDirectory 'audio_filters_test.c'))
        $testFlags += '-ffunction-sections', '-Wl,--gc-sections'
    }
    if ($test -eq 'network_socket') {
        $sources = @((Join-Path $testsDirectory 'network_socket_test.c'))
    }
    $elf = Join-Path $buildDirectory "${test}-test.elf"
    & $compiler @testFlags @sources -o $elf
    if ($LASTEXITCODE -ne 0) { throw "Test compilation failed: $test" }
    & $Python (Join-Path $testsDirectory 'run_overlay_test.py') $elf
    if ($LASTEXITCODE -ne 0) { throw "Test execution failed: $test" }
}
