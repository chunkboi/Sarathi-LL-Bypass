[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",

    [ValidateSet("23", "26")]
    [string]$Std = "26",

    [string]$OutDir = "",

    [switch]$Clean,
    [switch]$DynamicRuntime,
    [switch]$Werror,
    [switch]$Analyze,
    [switch]$Asan,
    [switch]$Test
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$sw = [Diagnostics.Stopwatch]::StartNew()

function Resolve-VsInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere.exe was not found. Install Visual Studio 2022/2026 with the 'Desktop development with C++' workload."
    }

    $install = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if ($LASTEXITCODE -ne 0 -or -not $install) {
        throw "No Visual Studio installation with the MSVC x64 toolchain was found."
    }

    return ($install | Select-Object -First 1).Trim()
}

function Import-VsDevEnvironment {
    param([string]$VsInstall)

    if ($env:VSCMD_ARG_TGT_ARCH -eq "x64" -and
        $env:VSCMD_ARG_HOST_ARCH -eq "x64" -and
        (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        return
    }

    $vsDevCmd = Join-Path $VsInstall "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat was not found under $VsInstall."
    }

    $env:VSCMD_SKIP_SENDTELEMETRY = "1"
    $dump = & cmd.exe /d /s /c "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE."
    }

    foreach ($line in $dump) {
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $eq), $line.Substring($eq + 1), "Process")
        }
    }
}

$vsInstall = Resolve-VsInstall
Import-VsDevEnvironment -VsInstall $vsInstall

$cl = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
if (-not $cl) {
    throw "cl.exe is not available after initializing the MSVC x64 environment."
}

$srcFile = Join-Path $PSScriptRoot "src\main.cpp"
if (-not (Test-Path -LiteralPath $srcFile)) {
    throw "Source file not found: $srcFile"
}

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $PSScriptRoot "build-msvc"
} elseif (-not [IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $PSScriptRoot $OutDir
}
$OutDir = [IO.Path]::GetFullPath($OutDir)

if ($Clean -and (Test-Path -LiteralPath $OutDir)) {
    Write-Host "Removing $OutDir"
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

$exePath = Join-Path $OutDir "seb_keys.exe"
$scratchDir = Join-Path ([IO.Path]::GetTempPath()) ("seb_keys-msvc-" + $PID)
New-Item -ItemType Directory -Path $scratchDir -Force | Out-Null

try {
    $objPath      = Join-Path $scratchDir "main.obj"
    $vcPdbPath    = Join-Path $scratchDir "vc.pdb"
    $manifestPath = Join-Path $scratchDir "seb_keys.manifest"

    $manifestXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
    </application>
  </compatibility>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
    </windowsSettings>
  </application>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="asInvoker" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
</assembly>
"@
    [IO.File]::WriteAllText($manifestPath, $manifestXml, [Text.UTF8Encoding]::new($false))

    $clArgs = [Collections.Generic.List[string]]@(
        "/nologo",
        "/utf-8",
        "/W4",
        "/diagnostics:caret",
        "/external:anglebrackets",
        "/external:W0",
        "/permissive-",
        "/EHsc",
        "/Zc:__cplusplus",
        "/Zc:preprocessor",
        "/Zc:inline",
        "/Zc:strictStrings",
        "/Zc:rvalueCast",
        "/Zc:enumTypes",
        "/Zc:templateScope",
        "/Zc:throwingNew",
        "/we4834",
        "/w14265",
        "/w15038",
        "/GS",
        "/sdl",
        "/Qspectre",
        "/ZH:SHA_256",
        "/Gy",
        "/Gw",
        "/Oi",
        "/DWIN32_LEAN_AND_MEAN",
        "/DNOMINMAX",
        "/DSTRICT",
        "/DUNICODE",
        "/D_UNICODE",
        "/D_WIN32_WINNT=0x0A00",
        "/DNTDDI_VERSION=0x0A000000",
        "/D_MSVC_STL_HARDENING=1",
        "/D_MSVC_STL_DESTRUCTOR_TOMBSTONES=1"
    )

    if ($Std -eq "26") {
        $clArgs.Add("/std:c++latest")
    } else {
        $clArgs.Add("/std:c++23preview")
    }

    $isDebug = ($Configuration -eq "Debug")
    if ($DynamicRuntime) {
        $clArgs.Add($(if ($isDebug) { "/MDd" } else { "/MD" }))
    } else {
        $clArgs.Add($(if ($isDebug) { "/MTd" } else { "/MT" }))
    }

    switch ($Configuration) {
        "Debug" {
            $clArgs.AddRange([string[]]@("/Od", "/Ob0", "/Zi", "/Fd$vcPdbPath"))
            if (-not $Asan) { $clArgs.Add("/RTC1") }
        }
        "Release" {
            $clArgs.AddRange([string[]]@("/O2", "/Ob3", "/DNDEBUG"))
            if (-not $Asan) { $clArgs.Add("/GL") }
        }
        "RelWithDebInfo" {
            $clArgs.AddRange([string[]]@("/O2", "/Ob2", "/Zi", "/Fd$vcPdbPath", "/DNDEBUG"))
        }
        "MinSizeRel" {
            $clArgs.AddRange([string[]]@("/O1", "/Ob2", "/DNDEBUG"))
            if (-not $Asan) { $clArgs.Add("/GL") }
        }
    }

    if (-not $Asan) {
        $clArgs.AddRange([string[]]@("/guard:cf", "/guard:ehcont"))
    } else {
        $clArgs.Add("/fsanitize=address")
    }

    if ($Werror) { $clArgs.Add("/WX") }
    if ($Analyze) { $clArgs.AddRange([string[]]@("/analyze", "/analyze:external-")) }

    $clArgs.AddRange([string[]]@(
        "/Fo$objPath",
        "/Fe$exePath",
        $srcFile,
        "/link",
        "/NOLOGO",
        "/SUBSYSTEM:CONSOLE",
        "/DEPENDENTLOADFLAG:0x800",
        "/DYNAMICBASE",
        "/HIGHENTROPYVA",
        "/NXCOMPAT",
        "/TSAWARE",
        "/Brepro",
        "/MANIFEST:EMBED",
        "/MANIFESTINPUT:$manifestPath",
        "/PDBALTPATH:seb_keys.pdb"
    ))

    if (-not $Asan) {
        $clArgs.AddRange([string[]]@("/GUARD:CF", "/GUARD:EHCONT", "/CETCOMPAT"))
    }

    if ($Configuration -eq "Debug" -or $Configuration -eq "RelWithDebInfo") {
        $pdbOut = Join-Path $OutDir "seb_keys.pdb"
        $clArgs.AddRange([string[]]@("/DEBUG", "/PDB:$pdbOut"))
    }

    if ($Configuration -ne "Debug") {
        $clArgs.AddRange([string[]]@("/RELEASE", "/OPT:REF", "/INCREMENTAL:NO"))
        if (-not $Asan) {
            $clArgs.Add("/OPT:ICF")
            if ($Configuration -eq "Release" -or $Configuration -eq "MinSizeRel") {
                $clArgs.Add("/LTCG")
            }
        }
    }

    $clArgs.AddRange([string[]]@(
        "advapi32.lib",
        "bcrypt.lib",
        "crypt32.lib",
        "version.lib",
        "ws2_32.lib"
    ))

    Write-Host "MSVC:  $cl"
    Write-Host "Build: $exePath ($Configuration, C++$Std)"

    & $cl @clArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    if (Test-Path -LiteralPath $scratchDir) {
        Remove-Item -LiteralPath $scratchDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Build completed, but seb_keys.exe was not found at $exePath"
}

if ($Test) {
    & $exePath --help | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Smoke test '--help' failed with exit code $LASTEXITCODE"
    }
    & $exePath 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
        throw "Smoke test (no args) unexpectedly succeeded with exit code 0"
    }
    Write-Host "Smoke tests passed."
}

Write-Host ("Built: {0} ({1:F2}s)" -f $exePath, $sw.Elapsed.TotalSeconds)