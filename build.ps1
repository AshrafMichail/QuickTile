param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [string]$Platform = 'x64',

    [switch]$SkipTests,

    [switch]$Analyze,

    [switch]$ClangTidy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$buildRoot = Join-Path $repoRoot 'build'
$solutionPath = Join-Path $repoRoot 'build\QuickTile.sln'
$vswherePath = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioRoot = 'C:\Program Files\Microsoft Visual Studio'
$windowsKitsIncludeRoot = 'C:\Program Files (x86)\Windows Kits\10\Include'
$quickTileExe = Join-Path $repoRoot ("build\{0}\QuickTile.exe" -f $Configuration)
$quickTileTestsExe = Join-Path $repoRoot ("build\{0}\QuickTileTests.exe" -f $Configuration)
$generatedVersionHeader = Join-Path $buildRoot 'generated_version.h'

function Get-VisualStudioInstallationPath {
    param(
        [string]$VswherePath,
        [string]$FallbackRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $candidate = $env:VSINSTALLDIR.TrimEnd('\')
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return $candidate
        }
    }

    if (Test-Path -LiteralPath $VswherePath -PathType Leaf) {
        $installationPath = & $VswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            return $installationPath.Trim()
        }
    }

    if (Test-Path -LiteralPath $FallbackRoot -PathType Container) {
        foreach ($versionDirectory in Get-ChildItem -LiteralPath $FallbackRoot -Directory | Sort-Object Name -Descending) {
            foreach ($editionDirectory in Get-ChildItem -LiteralPath $versionDirectory.FullName -Directory | Sort-Object Name) {
                $msbuildCandidate = Join-Path $editionDirectory.FullName 'MSBuild\Current\Bin\MSBuild.exe'
                if (Test-Path -LiteralPath $msbuildCandidate -PathType Leaf) {
                    return $editionDirectory.FullName
                }
            }
        }
    }

    throw 'Visual Studio with MSBuild was not found. Install Visual Studio Build Tools or a full Visual Studio edition with C++ support.'
}

function Get-FirstExistingPath {
    param(
        [string[]]$Candidates,
        [string]$Description
    )

    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }

    throw "$Description was not found. Checked: $($Candidates -join ', ')"
}

function Get-CMakePath {
    param(
        [string]$BuildRoot,
        [string]$FallbackPath
    )

    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) {
        return $cmakeCommand.Source
    }

    $cachePath = Join-Path $BuildRoot 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cacheLine = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
        if ($null -ne $cacheLine) {
            $candidate = $cacheLine.Matches[0].Groups[1].Value.Replace('/', '\\')
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    if (Test-Path -LiteralPath $FallbackPath -PathType Leaf) {
        return $FallbackPath
    }

    throw 'CMake was not found. Install CMake or ensure the Visual Studio bundled CMake is available.'
}

function Configure-Build {
    param(
        [string]$CMakePath,
        [string]$RepositoryRoot,
        [string]$BuildRoot,
        [bool]$EnableAnalyze
    )

    $analyzeValue = if ($EnableAnalyze) { 'ON' } else { 'OFF' }
    & $CMakePath -S $RepositoryRoot -B $BuildRoot "-DQUICKTILE_ENABLE_MSVC_ANALYZE=$analyzeValue"
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}

function Get-LatestVersionDirectory {
    param(
        [string]$RootPath,
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $RootPath -PathType Container)) {
        throw "$Description root was not found at '$RootPath'."
    }

    $directory = Get-ChildItem -LiteralPath $RootPath -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if ($null -eq $directory) {
        throw "No versioned directories were found under '$RootPath'."
    }

    return $directory.FullName
}

function Get-ClangTidyPath {
    param(
        [string[]]$FallbackPaths
    )

    $clangTidyCommand = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if ($null -ne $clangTidyCommand) {
        return $clangTidyCommand.Source
    }

    return Get-FirstExistingPath -Candidates $FallbackPaths -Description 'clang-tidy'
}

function Invoke-ClangTidy {
    param(
        [string]$RepositoryRoot,
        [string]$BuildRoot,
        [bool]$IncludeTests,
        [string]$VisualStudioInstallPath
    )

    $clangTidyPath = Get-ClangTidyPath -FallbackPaths @(
        (Join-Path $VisualStudioInstallPath 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'),
        (Join-Path $VisualStudioInstallPath 'VC\Tools\Llvm\bin\clang-tidy.exe')
    )
    $msvcIncludePath = Join-Path (Get-LatestVersionDirectory -RootPath (Join-Path $VisualStudioInstallPath 'VC\Tools\MSVC') -Description 'MSVC include') 'include'
    $windowsKitIncludePath = Get-LatestVersionDirectory -RootPath $windowsKitsIncludeRoot -Description 'Windows SDK include'
    $clangTidyConfigPath = Join-Path $RepositoryRoot '.clang-tidy'
    $pchPath = Join-Path $RepositoryRoot 'src\pch.h'

    if (-not (Test-Path -LiteralPath $clangTidyConfigPath -PathType Leaf)) {
        throw "clang-tidy config file was not found at '$clangTidyConfigPath'."
    }

    $commonArguments = @(
        "--config-file=$clangTidyConfigPath",
        '--quiet',
        '--extra-arg-before=--driver-mode=cl',
        '--extra-arg=/EHsc',
        '--extra-arg=/std:c++20',
        '--extra-arg=/DUNICODE',
        '--extra-arg=/D_UNICODE',
        '--extra-arg=/DNOMINMAX',
        '--extra-arg=/DWIN32_LEAN_AND_MEAN',
        "--extra-arg=/I$(Join-Path $RepositoryRoot 'src')",
        "--extra-arg=/I$BuildRoot",
        "--extra-arg=/FI$pchPath",
        "--extra-arg=/I$msvcIncludePath",
        "--extra-arg=/I$(Join-Path $windowsKitIncludePath 'ucrt')",
        "--extra-arg=/I$(Join-Path $windowsKitIncludePath 'shared')",
        "--extra-arg=/I$(Join-Path $windowsKitIncludePath 'um')",
        "--extra-arg=/I$(Join-Path $windowsKitIncludePath 'winrt')",
        "--extra-arg=/I$(Join-Path $windowsKitIncludePath 'cppwinrt')",
        '--'
    )

    $files = @(Get-ChildItem -LiteralPath (Join-Path $RepositoryRoot 'src') -Filter '*.cpp' -Recurse | Sort-Object FullName)
    if ($IncludeTests) {
        $files += Get-ChildItem -LiteralPath (Join-Path $RepositoryRoot 'tests') -Filter '*.cpp' -Recurse | Sort-Object FullName
    }

    foreach ($file in $files) {
        Write-Host "Running clang-tidy on $($file.FullName)"
        & $clangTidyPath $file.FullName @commonArguments
        if ($LASTEXITCODE -ne 0) {
            throw "clang-tidy failed for '$($file.FullName)' with exit code $LASTEXITCODE."
        }
    }
}

function Get-GitBuildVersion {
    param(
        [string]$RepositoryRoot
    )

    $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    if ($null -eq $gitCommand) {
        return 'unknown'
    }

    $nativeCommandPreference = Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue
    if ($null -ne $nativeCommandPreference) {
        Set-Variable -Name PSNativeCommandUseErrorActionPreference -Value $false
    }

    try {
        $hash = (& git -c core.safecrlf=false -C $RepositoryRoot rev-parse --short HEAD 2>$null)
        if ($LASTEXITCODE -ne 0) {
            return 'unknown'
        }

        $hash = $hash.Trim()
        if ([string]::IsNullOrWhiteSpace($hash)) {
            return 'unknown'
        }

        & git -c core.safecrlf=false -C $RepositoryRoot diff --quiet --ignore-submodules HEAD -- 2>$null
        $isDirty = $LASTEXITCODE -ne 0
    }
    finally {
        if ($null -ne $nativeCommandPreference) {
            Set-Variable -Name PSNativeCommandUseErrorActionPreference -Value $nativeCommandPreference.Value
        }
    }

    if ($isDirty) {
        return "$hash-dirty"
    }

    return $hash
}

function Write-VersionHeader {
    param(
        [string]$HeaderPath,
        [string]$BuildVersion
    )

    $headerContents = @"
#pragma once

namespace quicktile {

inline constexpr wchar_t kQuickTileBuildVersion[] = L"$BuildVersion";

}  // namespace quicktile
"@

    Set-Content -LiteralPath $HeaderPath -Value $headerContents -Encoding ascii
}

function Stop-QuickTileProcesses {
    $runningProcesses = Get-Process -Name 'QuickTile' -ErrorAction SilentlyContinue
    if ($null -eq $runningProcesses) {
        return
    }

    Write-Host 'Stopping running QuickTile.exe instances'
    $runningProcesses | Stop-Process -Force
}

$visualStudioInstallPath = Get-VisualStudioInstallationPath -VswherePath $vswherePath -FallbackRoot $visualStudioRoot
$msbuildPath = Get-FirstExistingPath -Candidates @(
    (Join-Path $visualStudioInstallPath 'MSBuild\Current\Bin\MSBuild.exe')
) -Description 'MSBuild'
$visualStudioCmakePath = Join-Path $visualStudioInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

$cmakePath = Get-CMakePath -BuildRoot $buildRoot -FallbackPath $visualStudioCmakePath
Configure-Build -CMakePath $cmakePath -RepositoryRoot $repoRoot -BuildRoot $buildRoot -EnableAnalyze $Analyze

if (-not (Test-Path -LiteralPath $solutionPath -PathType Leaf)) {
    throw "QuickTile solution not found at '$solutionPath'."
}

$buildVersion = Get-GitBuildVersion -RepositoryRoot $repoRoot
Write-VersionHeader -HeaderPath $generatedVersionHeader -BuildVersion $buildVersion

Write-Host "Version $buildVersion"
Stop-QuickTileProcesses

Write-Host "Building QuickTile.sln with $Configuration|$Platform"
if ($Analyze) {
    Write-Host 'MSVC /analyze enabled'
}
if ($ClangTidy) {
    Write-Host 'clang-tidy enabled'
}

& $msbuildPath $solutionPath /m /nologo "/t:QuickTile;QuickTileTests" "/p:Configuration=$Configuration" "/p:Platform=$Platform"
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $quickTileExe -PathType Leaf)) {
    throw "QuickTile executable not found at '$quickTileExe' after build."
}

if (-not (Test-Path -LiteralPath $quickTileTestsExe -PathType Leaf)) {
    throw "QuickTileTests executable not found at '$quickTileTestsExe' after build."
}

Write-Host "Built $quickTileExe"
Write-Host "Built $quickTileTestsExe"

if (-not $SkipTests) {
    Write-Host 'Running QuickTileTests.exe'
    & $quickTileTestsExe
    if ($LASTEXITCODE -ne 0) {
        throw "QuickTileTests failed with exit code $LASTEXITCODE."
    }
}

if ($ClangTidy) {
    Invoke-ClangTidy -RepositoryRoot $repoRoot -BuildRoot $buildRoot -IncludeTests (-not $SkipTests) -VisualStudioInstallPath $visualStudioInstallPath
}

Write-Host 'Build completed successfully.'
