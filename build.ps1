param([switch]$InjectMemoryFault)

$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot
$LoaderBuild = Join-Path $Root "build\loader"
$KernelBuild = Join-Path $Root "build\kernel"
$MirabootBuild = Join-Path $Root "build\miraboot"
$MemoryBuild = Join-Path $Root "build\memory"
$CompositorBuild = Join-Path $Root "build\compositor"
$InputBuild = Join-Path $Root "build\input"
$StorageBuild = Join-Path $Root "build\storage"
$FilesystemBuild = Join-Path $Root "build\filesystem"
$TaskbarBuild = Join-Path $Root "build\taskbar"
$PackerBuild = Join-Path $Root "build\rwlpack"
$ImageRoot = Join-Path $Root "build\image"
$BootDir = Join-Path $ImageRoot "boot"
$EfiDir = Join-Path $ImageRoot "EFI\BOOT"
$Toolchain = "C:\msys64\clang64\bin"
$Clang = Join-Path $Toolchain "clang++.exe"
$Lld = Join-Path $Toolchain "ld.lld.exe"
$ReadElf = Join-Path $Toolchain "llvm-readelf.exe"
$LimineDir = Join-Path $Root "third_party\limine"
$LoaderSource = Join-Path $Root "boot\loader\main.cpp"
$KernelSource = Join-Path $Root "kernel\src\main.cpp"
$RwlLoaderSource = Join-Path $Root "kernel\src\rwl_loader.cpp"
$PhysicalAllocatorSource = Join-Path $Root "kernel\src\memory\physical_allocator.cpp"
$MirabootSource = Join-Path $Root "miraboot\src\main.cpp"
$MemorySource = Join-Path $Root "memory\src\main.cpp"
$CompositorSource = Join-Path $Root "compositor\src\main.cpp"
$InputSource = Join-Path $Root "input\src\main.cpp"
$StorageSource = Join-Path $Root "storage\src\main.cpp"
$FilesystemSource = Join-Path $Root "filesystem\src\main.cpp"
$TaskbarSource = Join-Path $Root "taskbar\src\main.cpp"
$RuntimeSource = Join-Path $Root "kernel\src\runtime.cpp"
$ProcessManagerSource = Join-Path $Root "kernel\src\process\process_manager.cpp"
$SchedulerSource = Join-Path $Root "kernel\src\scheduler\scheduler.cpp"
$SchedulerRuntimeSource = Join-Path $Root "kernel\src\scheduler\runtime.cpp"
$InterruptSource = Join-Path $Root "kernel\arch\x86_64\interrupts.cpp"
$InterruptAssembly = Join-Path $Root "kernel\arch\x86_64\interrupts.S"
$ModuleCallAssembly = Join-Path $Root "kernel\arch\x86_64\module_call.S"
$PackerSource = Join-Path $Root "tools\rwlpack\main.cpp"
$RawlineInclude = Join-Path $Root "include"
$KernelInclude = Join-Path $Root "kernel\include"

foreach ($RequiredFile in @($Clang, $Lld, $ReadElf, (Join-Path $LimineDir "limine.h"),
    (Join-Path $LimineDir "BOOTX64.EFI"), $LoaderSource, $KernelSource, $ProcessManagerSource,
    $SchedulerSource, $SchedulerRuntimeSource, $InterruptSource, $InterruptAssembly, $ModuleCallAssembly, $PackerSource,
    $RwlLoaderSource, $PhysicalAllocatorSource, $MirabootSource, $MemorySource, $CompositorSource, $InputSource, $StorageSource, $FilesystemSource, $TaskbarSource, $RuntimeSource, (Join-Path $Root "kernel\src\storage.cpp"), (Join-Path $Root "kernel\arch\x86_64\mouse.cpp"), (Join-Path $Root "boot\loader\linker.ld"),
    (Join-Path $Root "kernel\linker.ld"), (Join-Path $Root "miraboot\linker.ld"),
    (Join-Path $Root "memory\linker.ld"), (Join-Path $Root "compositor\linker.ld"), (Join-Path $Root "input\linker.ld"), (Join-Path $Root "storage\linker.ld"), (Join-Path $Root "filesystem\linker.ld"), (Join-Path $Root "taskbar\linker.ld"))) {
    if (!(Test-Path $RequiredFile)) { throw "Required file not found: $RequiredFile" }
}

foreach ($Directory in @($LoaderBuild, $KernelBuild, $MirabootBuild, $MemoryBuild, $CompositorBuild, $InputBuild, $StorageBuild, $FilesystemBuild, $TaskbarBuild, $PackerBuild)) {
    New-Item -ItemType Directory -Force $Directory | Out-Null
}

$CommonCompileArgs = @("--target=x86_64-unknown-none-elf", "-std=c++23", "-ffreestanding", "-fno-common",
    "-fno-stack-protector", "-fno-stack-check", "-fno-pic", "-fno-pie", "-fno-rtti", "-fno-exceptions",
    "-ffunction-sections", "-fdata-sections", "-m64", "-march=x86-64", "-mno-mmx", "-mno-sse",
    "-mno-red-zone", "-mcmodel=kernel", "-Wall", "-Wextra", "-Wpedantic", "-I", $RawlineInclude,
    "-I", $KernelInclude)

Write-Host "Compiling loader and kernel..."
& $Clang @CommonCompileArgs -I $LimineDir -c $LoaderSource -o (Join-Path $LoaderBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Loader compilation failed." }
& $Clang @CommonCompileArgs -c $KernelSource -o (Join-Path $KernelBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel compilation failed." }
& $Clang @CommonCompileArgs -c $RuntimeSource -o (Join-Path $KernelBuild "runtime.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel runtime compilation failed." }
& $Clang @CommonCompileArgs -c $RwlLoaderSource -o (Join-Path $KernelBuild "rwl_loader.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel RWL loader compilation failed." }
& $Clang @CommonCompileArgs -c $PhysicalAllocatorSource -o (Join-Path $KernelBuild "physical_allocator.o")
if ($LASTEXITCODE -ne 0) { throw "Physical allocator compilation failed." }
& $Clang @CommonCompileArgs -c $ProcessManagerSource -o (Join-Path $KernelBuild "process_manager.o")
if ($LASTEXITCODE -ne 0) { throw "Process manager compilation failed." }
& $Clang @CommonCompileArgs -c $SchedulerSource -o (Join-Path $KernelBuild "scheduler.o")
if ($LASTEXITCODE -ne 0) { throw "Scheduler compilation failed." }
& $Clang @CommonCompileArgs -c $SchedulerRuntimeSource -o (Join-Path $KernelBuild "scheduler_runtime.o")
if ($LASTEXITCODE -ne 0) { throw "Scheduler runtime compilation failed." }
& $Clang @CommonCompileArgs -c $InterruptSource -o (Join-Path $KernelBuild "interrupts.o")
if ($LASTEXITCODE -ne 0) { throw "Interrupt compilation failed." }
& $Clang --target=x86_64-unknown-none-elf -I $RawlineInclude -I $KernelInclude -c $InterruptAssembly -o (Join-Path $KernelBuild "interrupts_asm.o")
if ($LASTEXITCODE -ne 0) { throw "Interrupt assembly compilation failed." }
& $Clang --target=x86_64-unknown-none-elf -I $RawlineInclude -I $KernelInclude -c $ModuleCallAssembly -o (Join-Path $KernelBuild "module_call.o")
if ($LASTEXITCODE -ne 0) { throw "RWL module call assembly compilation failed." }
& $Clang @CommonCompileArgs -c $MirabootSource -o (Join-Path $MirabootBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Miraboot compilation failed." }
$MemoryCompileArgs = $CommonCompileArgs
if ($InjectMemoryFault) { $MemoryCompileArgs += "-DRAWLINE_TEST_MEMORY_FAULT=1" }
& $Clang @MemoryCompileArgs -c $MemorySource -o (Join-Path $MemoryBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Memory module compilation failed." }
& $Clang @CommonCompileArgs -c $CompositorSource -o (Join-Path $CompositorBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Compositor module compilation failed." }
& $Clang @CommonCompileArgs -c (Join-Path $Root "kernel\src\display.cpp") -o (Join-Path $KernelBuild "display.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel display layer compilation failed." }
& $Clang @CommonCompileArgs -c (Join-Path $Root "kernel\src\graphics.cpp") -o (Join-Path $KernelBuild "graphics.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel graphics backend compilation failed." }
& $Clang @CommonCompileArgs -c (Join-Path $Root "kernel\src\virtio_gpu.cpp") -o (Join-Path $KernelBuild "virtio_gpu.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel VirtIO-GPU compilation failed." }
& $Clang @CommonCompileArgs -c (Join-Path $Root "kernel\arch\x86_64\mouse.cpp") -o (Join-Path $KernelBuild "mouse.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel PS/2 mouse layer compilation failed." }
& $Clang @CommonCompileArgs -c $InputSource -o (Join-Path $InputBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Input module compilation failed." }
& $Clang @CommonCompileArgs -c $StorageSource -o (Join-Path $StorageBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Storage module compilation failed." }
& $Clang @CommonCompileArgs -c $FilesystemSource -o (Join-Path $FilesystemBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Filesystem module compilation failed." }
& $Clang @CommonCompileArgs -c $TaskbarSource -o (Join-Path $TaskbarBuild "main.o")
if ($LASTEXITCODE -ne 0) { throw "Taskbar module compilation failed." }
& $Clang @CommonCompileArgs -c $RuntimeSource -o (Join-Path $FilesystemBuild "runtime.o")
if ($LASTEXITCODE -ne 0) { throw "Filesystem runtime compilation failed." }
& $Clang @CommonCompileArgs -c $RuntimeSource -o (Join-Path $StorageBuild "runtime.o")
if ($LASTEXITCODE -ne 0) { throw "Storage runtime compilation failed." }
& $Clang @CommonCompileArgs -c (Join-Path $Root "kernel\src\storage.cpp") -o (Join-Path $KernelBuild "storage.o")
if ($LASTEXITCODE -ne 0) { throw "Kernel VirtIO block layer compilation failed." }

Write-Host "Linking separate ELF intermediates..."
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "boot\loader\linker.ld") (Join-Path $LoaderBuild "main.o") `
    -o (Join-Path $LoaderBuild "loader.elf")
if ($LASTEXITCODE -ne 0) { throw "Loader linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "kernel\linker.ld") (Join-Path $KernelBuild "main.o") `
    (Join-Path $KernelBuild "runtime.o") `
    (Join-Path $KernelBuild "rwl_loader.o") `
    (Join-Path $KernelBuild "physical_allocator.o") `
    (Join-Path $KernelBuild "process_manager.o") `
    (Join-Path $KernelBuild "scheduler.o") `
    (Join-Path $KernelBuild "scheduler_runtime.o") `
    (Join-Path $KernelBuild "interrupts.o") `
    (Join-Path $KernelBuild "interrupts_asm.o") `
    (Join-Path $KernelBuild "module_call.o") `
    (Join-Path $KernelBuild "display.o") `
    (Join-Path $KernelBuild "graphics.o") `
    (Join-Path $KernelBuild "virtio_gpu.o") `
    (Join-Path $KernelBuild "mouse.o") `
    (Join-Path $KernelBuild "storage.o") `
    -o (Join-Path $KernelBuild "kernel.elf")
if ($LASTEXITCODE -ne 0) { throw "Kernel linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "miraboot\linker.ld") (Join-Path $MirabootBuild "main.o") (Join-Path $KernelBuild "runtime.o") `
    -o (Join-Path $MirabootBuild "miraboot.elf")
if ($LASTEXITCODE -ne 0) { throw "Miraboot linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "memory\linker.ld") (Join-Path $MemoryBuild "main.o") (Join-Path $KernelBuild "runtime.o") `
    -o (Join-Path $MemoryBuild "memory.elf")
if ($LASTEXITCODE -ne 0) { throw "Memory module linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "compositor\linker.ld") (Join-Path $CompositorBuild "main.o") (Join-Path $KernelBuild "runtime.o") `
    -o (Join-Path $CompositorBuild "compositor.elf")
if ($LASTEXITCODE -ne 0) { throw "Compositor module linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "input\linker.ld") (Join-Path $InputBuild "main.o") (Join-Path $KernelBuild "runtime.o") `
    -o (Join-Path $InputBuild "input.elf")
if ($LASTEXITCODE -ne 0) { throw "Input module linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "storage\linker.ld") (Join-Path $StorageBuild "main.o") `
    (Join-Path $StorageBuild "runtime.o") `
    -o (Join-Path $StorageBuild "storage.elf")
if ($LASTEXITCODE -ne 0) { throw "Storage linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "filesystem\linker.ld") (Join-Path $FilesystemBuild "main.o") `
    (Join-Path $FilesystemBuild "runtime.o") `
    -o (Join-Path $FilesystemBuild "filesystem.elf")
if ($LASTEXITCODE -ne 0) { throw "Filesystem linking failed." }
& $Lld -m elf_x86_64 -static -z max-page-size=0x1000 -z noexecstack --gc-sections --build-id=sha1 `
    -T (Join-Path $Root "taskbar\linker.ld") (Join-Path $TaskbarBuild "main.o") `
    -o (Join-Path $TaskbarBuild "taskbar.elf")
if ($LASTEXITCODE -ne 0) { throw "Taskbar linking failed." }

Write-Host "Building host RWL packer..."
& $Clang -std=c++23 -Wall -Wextra -Wpedantic -I $RawlineInclude $PackerSource -o (Join-Path $PackerBuild "rwlpack.exe")
if ($LASTEXITCODE -ne 0) { throw "RWL packer compilation failed." }
if ($env:Path -notlike "*$Toolchain*") { $env:Path = "$Toolchain;$env:Path" }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $KernelBuild "kernel.elf") (Join-Path $KernelBuild "kernel.rwl")
if ($LASTEXITCODE -ne 0) { throw "RWL packing failed." }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $MirabootBuild "miraboot.elf") (Join-Path $MirabootBuild "miraboot.rwl")
if ($LASTEXITCODE -ne 0) { throw "Miraboot RWL packing failed." }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $MemoryBuild "memory.elf") (Join-Path $MemoryBuild "memory.rwl")
if ($LASTEXITCODE -ne 0) { throw "Memory RWL packing failed." }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $CompositorBuild "compositor.elf") (Join-Path $CompositorBuild "compositor.rwl")
if ($LASTEXITCODE -ne 0) { throw "Compositor RWL packing failed." }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $InputBuild "input.elf") (Join-Path $InputBuild "input.rwl")
if ($LASTEXITCODE -ne 0) { throw "Input RWL packing failed." }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $StorageBuild "storage.elf") (Join-Path $StorageBuild "storage.rwl")
if ($LASTEXITCODE -ne 0) { throw "Storage RWL packing failed." }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $FilesystemBuild "filesystem.elf") (Join-Path $FilesystemBuild "filesystem.rwl")
if ($LASTEXITCODE -ne 0) { throw "Filesystem RWL packing failed." }
& (Join-Path $PackerBuild "rwlpack.exe") (Join-Path $TaskbarBuild "taskbar.elf") (Join-Path $TaskbarBuild "taskbar.rwl")
if ($LASTEXITCODE -ne 0) { throw "Taskbar RWL packing failed." }

Write-Host "Staging boot tree..."
Remove-Item $ImageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $BootDir, $EfiDir | Out-Null
Copy-Item (Join-Path $LoaderBuild "loader.elf") (Join-Path $BootDir "loader.elf")
Copy-Item (Join-Path $KernelBuild "kernel.rwl") (Join-Path $BootDir "kernel.rwl")
Copy-Item (Join-Path $MirabootBuild "miraboot.rwl") (Join-Path $BootDir "miraboot.rwl")
Copy-Item (Join-Path $MemoryBuild "memory.rwl") (Join-Path $BootDir "memory.rwl")
Copy-Item (Join-Path $CompositorBuild "compositor.rwl") (Join-Path $BootDir "compositor.rwl")
Copy-Item (Join-Path $InputBuild "input.rwl") (Join-Path $BootDir "input.rwl")
Copy-Item (Join-Path $StorageBuild "storage.rwl") (Join-Path $BootDir "storage.rwl")
Copy-Item (Join-Path $FilesystemBuild "filesystem.rwl") (Join-Path $BootDir "filesystem.rwl")
Copy-Item (Join-Path $TaskbarBuild "taskbar.rwl") (Join-Path $BootDir "taskbar.rwl")
Copy-Item (Join-Path $LimineDir "BOOTX64.EFI") $EfiDir
Copy-Item (Join-Path $LimineDir "limine-uefi-cd.bin") $BootDir
Copy-Item (Join-Path $LimineDir "limine-bios-cd.bin") $BootDir
Copy-Item (Join-Path $LimineDir "limine-bios.sys") $BootDir
Copy-Item (Join-Path $Root "limine.conf") $ImageRoot

Write-Host "Loader ELF header:"
& $ReadElf -h (Join-Path $BootDir "loader.elf")
if ($LASTEXITCODE -ne 0) { throw "Loader inspection failed." }
Write-Host "`nStaged artifacts:`n  $BootDir\loader.elf`n  $BootDir\kernel.rwl`n  $BootDir\miraboot.rwl`n  $BootDir\memory.rwl`n  $BootDir\storage.rwl`n  $BootDir\filesystem.rwl`n  $BootDir\compositor.rwl`n  $BootDir\taskbar.rwl`n  $BootDir\input.rwl"
