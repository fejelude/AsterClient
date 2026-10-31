[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build',
    [string]$OutputDirectory = 'dist'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakePath = Join-Path $PSScriptRoot 'vendor/cmake-3.31.8-windows-x86_64/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
    $cmakePath = (Get-Command cmake -ErrorAction Stop).Source
}
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
foreach ($binary in @('AsterClient.exe', 'AsterBridge.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot "Release/$binary") -PathType Leaf)) {
        throw "Build Release first: missing $binary."
    }
}

$packageName = 'AsterClient-Windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$packageRoot = Join-Path $outputRoot $packageName
$archivePath = $packageRoot + '.zip'
if ((Test-Path -LiteralPath $packageRoot) -or (Test-Path -LiteralPath $archivePath)) {
    throw 'Package destination already exists; existing files are preserved.'
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
& $cmakePath --install $buildRoot --config Release --prefix $packageRoot
if ($LASTEXITCODE -ne 0) { throw 'CMake install failed; no archive was created.' }

$utf8 = New-Object System.Text.UTF8Encoding($false)
$manifest = foreach ($file in Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName) {
    $relative = $file.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $relative
}
[IO.File]::WriteAllLines((Join-Path $packageRoot 'SHA256SUMS.txt'), [string[]]$manifest, $utf8)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $archivePath, [IO.Compression.CompressionLevel]::Optimal, $true)
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archivePath + '.sha256'), ($archiveHash + '  ' + [IO.Path]::GetFileName($archivePath) + "`n"), $utf8)
[pscustomobject]@{ Folder = $packageRoot; Archive = $archivePath; SHA256 = $archiveHash }
