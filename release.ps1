Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$buildOutput = Join-Path $repoRoot 'build\Release\QuickTile.exe'
$readmePath = Join-Path $repoRoot 'README.md'
$dateStamp = Get-Date -Format 'yyyy-MM-dd'
$gitHash = (& git -C $repoRoot rev-parse --short HEAD).Trim()

if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gitHash)) {
    throw 'Failed to determine git hash for release archive name.'
}

$zipPath = Join-Path $repoRoot ("QuickTile_{0}_{1}.zip" -f $dateStamp, $gitHash)

if (-not (Test-Path -LiteralPath $buildOutput -PathType Leaf)) {
    throw "QuickTile executable not found at '$buildOutput'. Build the Release target first."
}

if (-not (Test-Path -LiteralPath $readmePath -PathType Leaf)) {
    throw "README.md not found at '$readmePath'."
}

if (Test-Path -LiteralPath $zipPath -PathType Leaf) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -LiteralPath @($buildOutput, $readmePath) -DestinationPath $zipPath
Write-Host "Created $zipPath"