param(
    [ValidateSet("Release")]
    [string]$Configuration = "Release",
    [switch]$SimTouch,
    [switch]$NoClean,
    [switch]$NoVersionUpdate,
    [switch]$KeepSimBuildArtifacts
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RepoRoot $Configuration
$ToolRoot = "C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins"

function Find-Tool {
    param(
        [string]$CommandName,
        [string]$SearchRoot
    )

    $cmd = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($cmd -ne $null) {
        return $cmd.Source
    }

    $found = Get-ChildItem -Path $SearchRoot -Recurse -Filter $CommandName -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ([string]::IsNullOrWhiteSpace($found)) {
        throw "Cannot find $CommandName. Install STM32CubeIDE or add GNU ARM tools to PATH."
    }
    return $found
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$FilePath failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Remove-BuildOutputs {
    param(
        [string]$Directory
    )

    $OutputNames = @(
        "Curva_G431_Mai.bin",
        "Curva_G431_Mai.elf",
        "Curva_G431_Mai.list",
        "Curva_G431_Mai.map",
        "default.size.stdout"
    )

    foreach ($Name in $OutputNames) {
        Remove-Item -LiteralPath (Join-Path $Directory $Name) -Force -ErrorAction SilentlyContinue
    }
}

$Gcc = Find-Tool "arm-none-eabi-gcc.exe" $ToolRoot
$Make = Find-Tool "make.exe" $ToolRoot
$ToolBin = Split-Path $Gcc -Parent
$MakeBin = Split-Path $Make -Parent
$env:Path = "$ToolBin;$MakeBin;$env:Path"

if (-not $NoVersionUpdate) {
    Invoke-Checked "powershell.exe" @(
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "update_version.ps1")
    ) $RepoRoot
}

if (-not $NoClean) {
    Invoke-Checked $Make @("-C", $Configuration, "clean") $RepoRoot
}

Invoke-Checked $Make @("-C", $Configuration, "all") $RepoRoot

if ($SimTouch) {
    $Source = Join-Path $RepoRoot "Core\Src\capsense_sim.c"
    $Object = Join-Path $BuildDir "Core\Src\capsense_sim.o"
    $Dep = Join-Path $BuildDir "Core\Src\capsense_sim.d"
    $RootSlash = ($RepoRoot.Path -replace "\\", "/")
    $ObjectSlash = ($Object -replace "\\", "/")
    $DepSlash = ($Dep -replace "\\", "/")

    $SimArgs = @(
        ($Source -replace "\\", "/"),
        "-mcpu=cortex-m4",
        "-std=gnu18",
        "-DUSE_HAL_DRIVER",
        "-DSTM32G431xx",
        "-DSTM32_THREAD_SAFE_STRATEGY=4",
        "-DCAPSENSE_SIMULATED_TOUCH_ENABLE=1",
        "-c",
        "-I$RootSlash/Core/Inc",
        "-I$RootSlash/Drivers/STM32G4xx_HAL_Driver/Inc",
        "-I$RootSlash/Drivers/STM32G4xx_HAL_Driver/Inc/Legacy",
        "-I$RootSlash/Drivers/CMSIS/Device/ST/STM32G4xx/Include",
        "-I$RootSlash/Drivers/CMSIS/Include",
        "-I$RootSlash/Middlewares/Third_Party/FreeRTOS/Source/include",
        "-I$RootSlash/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS",
        "-I$RootSlash/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F",
        "-I$RootSlash/Composite",
        "-I$RootSlash/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/COMPOSITE/Inc",
        "-I$RootSlash/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/HID_KEYBOARD/Inc",
        "-I$RootSlash/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/HID_CUSTOM/Inc",
        "-I$RootSlash/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Core/Inc",
        "-I$RootSlash/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/App",
        "-I$RootSlash/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Target",
        "-I$RootSlash/Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE/Class/CDC_ACM/Inc",
        "-I$RootSlash/Core/ThreadSafe",
        "-Os",
        "-ffunction-sections",
        "-fdata-sections",
        "-Wall",
        "-fstack-usage",
        "-fcyclomatic-complexity",
        "-MMD",
        "-MP",
        "-MF$DepSlash",
        "-MT$ObjectSlash",
        "--specs=nano.specs",
        "-mfpu=fpv4-sp-d16",
        "-mfloat-abi=hard",
        "-mthumb",
        "-o",
        $ObjectSlash
    )
    Invoke-Checked $Gcc $SimArgs $RepoRoot
    Remove-BuildOutputs $BuildDir
    Invoke-Checked $Make @("-C", $Configuration, "all") $RepoRoot
}

$InputBin = Join-Path $BuildDir "Curva_G431_Mai.bin"
$PatchName = "Curva_G431_Mai_Patched.bin"
if ($SimTouch) {
    $PatchName = "Curva_G431_Mai_Sim_Patched.bin"
}
$OutputBin = Join-Path $BuildDir $PatchName

$Py = Get-Command "py.exe" -ErrorAction SilentlyContinue
if ($Py -ne $null) {
    Invoke-Checked $Py.Source @((Join-Path $PSScriptRoot "patch_firmware.py"), $InputBin, $OutputBin) $RepoRoot
} else {
    $Python = Get-Command "python.exe" -ErrorAction Stop
    Invoke-Checked $Python.Source @((Join-Path $PSScriptRoot "patch_firmware.py"), $InputBin, $OutputBin) $RepoRoot
}

Write-Host "Built firmware: $OutputBin"

if ($SimTouch -and -not $KeepSimBuildArtifacts) {
    Write-Host "Restoring default non-sim build artifacts..."
    $DefaultArgs = @($SimArgs | Where-Object { $_ -ne "-DCAPSENSE_SIMULATED_TOUCH_ENABLE=1" })
    Invoke-Checked $Gcc $DefaultArgs $RepoRoot
    Remove-BuildOutputs $BuildDir
    Invoke-Checked $Make @("-C", $Configuration, "all") $RepoRoot
}
