param(
    [string]$JuceSourceDir = '',
    [string]$BuildDir = "$PSScriptRoot/build",
    [string]$LanguageAssetsDir = "$BuildDir/intent-language"
)
$ErrorActionPreference = 'Stop'
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
& python (Join-Path $PSScriptRoot 'fetch-language-assets.py') $LanguageAssetsDir
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare MiniLM assets.' }
$LanguageAssetsDir = (Resolve-Path -LiteralPath $LanguageAssetsDir).Path
$configureArgs = @('-S', $PSScriptRoot, '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DINTENT_LANGUAGE_ASSETS=$LanguageAssetsDir")
if ($JuceSourceDir) { $configureArgs += "-DJUCE_SOURCE_DIR=$JuceSourceDir" }
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $BuildDir --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
& $ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
Write-Output "Build and tests passed. Artifacts: $BuildDir/Intent_artefacts/Release"
& (Join-Path $PSScriptRoot 'publish-windows.ps1') -ArtifactsDir (Join-Path $BuildDir 'Intent_artefacts/Release')
