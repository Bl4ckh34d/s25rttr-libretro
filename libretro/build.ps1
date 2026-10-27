param(
    [string]$Ndk = $env:ANDROID_NDK_HOME,
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [string]$Upstream = '',
    [string]$SampleRateSource = '',
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Ndk) { throw 'Set ANDROID_NDK_HOME or pass -Ndk (tested with NDK r27d).' }
if (!$Upstream) { $Upstream = Join-Path $root 'deps/s25rttr-android' }
$options = @('-S', $PSScriptRoot, '-B', "$root/build/libretro", '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DPORT_ROOT=$Upstream",
    "-DCMAKE_TOOLCHAIN_FILE=$Ndk/build/cmake/android.toolchain.cmake",
    '-DANDROID_ABI=armeabi-v7a', '-DANDROID_PLATFORM=android-28',
    '-DANDROID_STL=c++_static', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_WARN_DEPRECATED=OFF')
if ($SampleRateSource) { $options += "-DFETCHCONTENT_SOURCE_DIR_SAMPLERATE=$SampleRateSource" }
& $CMake @options
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& $CMake --build "$root/build/libretro" --target rttr_libretro --parallel $Jobs
if ($LASTEXITCODE) { throw 'Core build failed' }
New-Item -ItemType Directory -Path "$root/release" -Force | Out-Null
& "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" --strip-unneeded `
    -o "$root/release/rttr_libretro_android.so" "$root/build/libretro/rttr_libretro.so"
if ($LASTEXITCODE) { throw 'Core strip failed' }
