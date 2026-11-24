param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactsDir
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $ArtifactsDir).Path
$bundle = Join-Path $source 'VST3/Intent.vst3'
$binary = Join-Path $bundle 'Contents/x86_64-win/Intent.vst3'
$manifest = Join-Path $bundle 'Contents/Resources/moduleinfo.json'
$standalone = Join-Path $source 'Standalone/Intent.exe'
foreach ($file in @($binary, $manifest, $standalone)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
        throw "Missing or empty Release artifact: $file"
    }
}

# JUCE's manifest can contain trailing commas, so read the top-level version field directly.
$versionMatch = [regex]::Match((Get-Content -LiteralPath $manifest -Raw), '"Version"\s*:\s*"([0-9]+\.[0-9]+\.[0-9]+)"')
if (!$versionMatch.Success) { throw "Could not read the Intent release version from $manifest" }
$version = $versionMatch.Groups[1].Value
$language = 'Intent.vst3/Contents/Resources/IntentLanguage'

$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$releases = Join-Path $project 'releases'
$current = Join-Path $releases 'windows-x64'
$history = Join-Path $releases '.history'
New-Item -ItemType Directory -Path $history -Force | Out-Null
$stamp = [Guid]::NewGuid().ToString('N')
$stage = Join-Path $history "staging-$stamp"
$previous = Join-Path $history "previous-$stamp"

# Serialise publishes. Stage and back up outside REAPER's scan path.
$lock = [System.IO.File]::Open((Join-Path $history 'publish.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    $requiredFiles = @(
        @{ Source = $binary; Relative = 'Intent.vst3/Contents/x86_64-win/Intent.vst3' }
        @{ Source = $manifest; Relative = 'Intent.vst3/Contents/Resources/moduleinfo.json' }
        @{ Source = $standalone; Relative = 'Intent.exe' }
    )
    if ([version]$version -ge [version]'0.3.0') {
        foreach ($name in @('model.onnx', 'vocab.txt', 'onnxruntime.dll',
            'ONNX-Runtime-LICENSE.txt', 'ONNX-Runtime-ThirdPartyNotices.txt', 'MiniLM-LICENSE.txt')) {
            $relative = "$language/$name"
            $asset = Join-Path (Split-Path $bundle) $relative
            if (!(Test-Path -LiteralPath $asset -PathType Leaf) -or (Get-Item -LiteralPath $asset).Length -eq 0) {
                throw "Missing or empty language asset: $asset"
            }
            $requiredFiles += @{ Source = $asset; Relative = $relative }
        }
    }
    if ([version]$version -ge [version]'0.6.0') {
        $notices = Join-Path $PSScriptRoot 'third_party/libebur128'
        $requiredFiles += @(
            @{ Source = (Join-Path $notices 'COPYING'); Relative = 'Intent.vst3/Contents/Resources/libebur128-LICENSE.txt' }
            @{ Source = (Join-Path $notices 'QUEUE-LICENSE.txt'); Relative = 'Intent.vst3/Contents/Resources/libebur128-Queue-LICENSE.txt' }
        )
    }
    $alreadyPublished = $true
    foreach ($file in $requiredFiles) {
        $installed = Join-Path $current $file.Relative
        $file.Hash = (Get-FileHash -LiteralPath $file.Source -Algorithm SHA256).Hash
        if (!(Test-Path -LiteralPath $installed -PathType Leaf) -or
            (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne $file.Hash) {
            $alreadyPublished = $false
        }
    }
    if ($alreadyPublished) {
        Write-Output "Intent $version is already published at $current"
        return
    }

    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -LiteralPath $bundle -Destination (Join-Path $stage 'Intent.vst3') -Recurse
    Copy-Item -LiteralPath $standalone -Destination (Join-Path $stage 'Intent.exe')
    if ([version]$version -ge [version]'0.6.0') {
        $resources = Join-Path $stage 'Intent.vst3/Contents/Resources'
        Copy-Item -LiteralPath (Join-Path $notices 'COPYING') -Destination (Join-Path $resources 'libebur128-LICENSE.txt')
        Copy-Item -LiteralPath (Join-Path $notices 'QUEUE-LICENSE.txt') -Destination (Join-Path $resources 'libebur128-Queue-LICENSE.txt')
    }
    @(
        "Intent $version / Windows x64"
        'Intent.vst3 is the complete REAPER plugin bundle.'
        'Intent.exe is the standalone live-input effect.'
        'Keep bundled language files with the VST3, including for standalone use.'
        'This is the stable release folder. Keep this path in the REAPER VST scan list.'
    ) | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding UTF8

    foreach ($file in $requiredFiles) {
        $stagedHash = (Get-FileHash -LiteralPath (Join-Path $stage $file.Relative) -Algorithm SHA256).Hash
        if ($file.Hash -ne $stagedHash) { throw "Staged file failed verification: $($file.Relative)" }
    }

    $files = Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName
    $files | ForEach-Object {
        $digest = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $relative = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        "$digest  $relative"
    } | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII

    $movedPrevious = $false
    try {
        if (Test-Path -LiteralPath $current) {
            [System.IO.Directory]::Move($current, $previous)
            $movedPrevious = $true
        }
        [System.IO.Directory]::Move($stage, $current)
    } catch {
        if ($movedPrevious) { [System.IO.Directory]::Move($previous, $current) }
        throw "Could not replace the master release. If REAPER or Intent Standalone is open, close it and rerun this script. $($_.Exception.Message)"
    }
    Write-Output "Published Intent $version to $current"
} finally {
    $lock.Dispose()
}
