$ErrorActionPreference = "Stop"

$source = "C:\Users\bruno\Documents\Codex\2026-05-29\ableton-codex-https-producer-pal-org\peakeater-spectral\build\Release\peakeater_spectral_artefacts\Release\VST3\Peakeater Spectral Beta.vst3"
$targetRoot = "C:\Program Files\Common Files\VST3"
$betaTarget = Join-Path $targetRoot "Peakeater Spectral Beta.vst3"
$driveSafeTarget = Join-Path $targetRoot "Peakeater Spectral Beta DriveSafe.vst3"
$pendingFile = Join-Path $betaTarget "Contents\x86_64-win\Peakeater Spectral Beta.vst3.pending"
$logPath = "C:\Users\bruno\Documents\Codex\2026-05-29\ableton-codex-https-producer-pal-org\peakeater-spectral\tools\peakeater-beta-vst3-update.log"

"$(Get-Date -Format o) Starting Peakeater Spectral Beta VST3 update" | Out-File -LiteralPath $logPath -Encoding UTF8

if (-not (Test-Path -LiteralPath $source)) {
    throw "Source VST3 bundle not found: $source"
}

$resolvedSource = (Resolve-Path -LiteralPath $source).Path
$resolvedTargetRoot = (Resolve-Path -LiteralPath $targetRoot).Path
if (-not $resolvedSource.StartsWith("C:\Users\bruno\Documents\Codex\2026-05-29\ableton-codex-https-producer-pal-org\peakeater-spectral")) {
    throw "Source path is outside the expected workspace: $resolvedSource"
}

foreach ($target in @($betaTarget, $driveSafeTarget)) {
    $fullTarget = [System.IO.Path]::GetFullPath($target)
    if (-not $fullTarget.StartsWith($resolvedTargetRoot)) {
        throw "Target path is outside VST3 root: $target"
    }
}

if (Test-Path -LiteralPath $pendingFile) {
    Remove-Item -LiteralPath $pendingFile -Force
    "$(Get-Date -Format o) Removed pending DLL" | Out-File -LiteralPath $logPath -Append -Encoding UTF8
}

if (Test-Path -LiteralPath $betaTarget) {
    Remove-Item -LiteralPath $betaTarget -Recurse -Force
    "$(Get-Date -Format o) Removed old Beta bundle" | Out-File -LiteralPath $logPath -Append -Encoding UTF8
}

Copy-Item -LiteralPath $resolvedSource -Destination $targetRoot -Recurse -Force
"$(Get-Date -Format o) Installed updated Beta bundle" | Out-File -LiteralPath $logPath -Append -Encoding UTF8

if (Test-Path -LiteralPath $driveSafeTarget) {
    Remove-Item -LiteralPath $driveSafeTarget -Recurse -Force
    "$(Get-Date -Format o) Removed DriveSafe duplicate bundle" | Out-File -LiteralPath $logPath -Append -Encoding UTF8
}

$betaInfo = Get-ChildItem -LiteralPath $betaTarget -Recurse -File | Measure-Object -Property Length -Sum
"$(Get-Date -Format o) Done. BetaFiles=$($betaInfo.Count) BetaSize=$($betaInfo.Sum)" | Out-File -LiteralPath $logPath -Append -Encoding UTF8
