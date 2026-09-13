param(
    [string] $PackageRoot = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [switch] $SystemInstall
)

$ErrorActionPreference = 'Stop'

if ($SystemInstall) {
    $vst3Destination = Join-Path ${env:CommonProgramFiles} 'VST3'
    $clapDestination = Join-Path ${env:CommonProgramFiles} 'CLAP'
} else {
    $vst3Destination = Join-Path ${env:LOCALAPPDATA} 'Programs\Common\VST3'
    $clapDestination = Join-Path ${env:USERPROFILE} '.clap'
}

$vst3 = Get-ChildItem -Path $PackageRoot -Recurse -Directory -Filter '*.vst3' |
    Sort-Object @{ Expression = { if ($_.Name -match 'Spectral 3') { 0 } else { 1 } } }, FullName |
    Select-Object -First 1
$clap = Get-ChildItem -Path $PackageRoot -Recurse -File -Filter '*.clap' |
    Sort-Object @{ Expression = { if ($_.Name -match 'Spectral 3') { 0 } else { 1 } } }, FullName |
    Select-Object -First 1

if (-not $vst3 -and -not $clap) {
    throw "No Peakeater Spectral VST3 or CLAP artifact was found below '$PackageRoot'."
}

New-Item -ItemType Directory -Force -Path $vst3Destination, $clapDestination | Out-Null

if ($vst3) {
    $target = Join-Path $vst3Destination $vst3.Name
    if (Test-Path $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Copy-Item -LiteralPath $vst3.FullName -Destination $target -Recurse
    Write-Host "Installed VST3: $target"
}

if ($clap) {
    $target = Join-Path $clapDestination 'Peakeater Spectral.clap'
    Copy-Item -LiteralPath $clap.FullName -Destination $target -Force
    Write-Host "Installed CLAP: $target"
}

Write-Host 'Restart the DAW and rescan plug-ins if the plug-in is not visible.'
