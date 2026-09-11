$ErrorActionPreference = "Stop"

$sourceDll = "C:\Users\bruno\Documents\Codex\2026-05-29\ableton-codex-https-producer-pal-org\peakeater-spectral\build\Release\peakeater_spectral_artefacts\Release\VST3\Peakeater Spectral Beta.vst3\Contents\x86_64-win\Peakeater Spectral Beta.vst3"
$targetDll = "C:\Program Files\Common Files\VST3\Peakeater Spectral Beta.vst3\Contents\x86_64-win\Peakeater Spectral Beta.vst3"
$pendingDll = "$targetDll.pending"
$logPath = "C:\Users\bruno\Documents\Codex\2026-05-29\ableton-codex-https-producer-pal-org\peakeater-spectral\tools\peakeater-beta-vst3-update.log"

trap {
    "$(Get-Date -Format o) ERROR $($_.Exception.Message)" | Out-File -LiteralPath $logPath -Append -Encoding UTF8
    exit 1
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class PendingFileRename
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool MoveFileEx(string lpExistingFileName, string lpNewFileName, int dwFlags);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode, EntryPoint = "MoveFileExW")]
    public static extern bool MoveFileExDelete(string lpExistingFileName, IntPtr lpNewFileName, int dwFlags);
}
"@

$MOVEFILE_DELAY_UNTIL_REBOOT = 0x00000004
"$(Get-Date -Format o) Starting pending reboot VST3 update" | Out-File -LiteralPath $logPath -Append -Encoding UTF8

if (-not (Test-Path -LiteralPath $sourceDll)) {
    throw "Source DLL not found: $sourceDll"
}

Copy-Item -LiteralPath $sourceDll -Destination $pendingDll -Force
"$(Get-Date -Format o) Copied pending DLL: $pendingDll" | Out-File -LiteralPath $logPath -Append -Encoding UTF8

$deleteOk = [PendingFileRename]::MoveFileExDelete($targetDll, [IntPtr]::Zero, $MOVEFILE_DELAY_UNTIL_REBOOT)
if (-not $deleteOk) {
    throw "Failed to schedule target DLL delete. Win32Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
"$(Get-Date -Format o) Scheduled target DLL delete" | Out-File -LiteralPath $logPath -Append -Encoding UTF8

$replaceOk = [PendingFileRename]::MoveFileEx($pendingDll, $targetDll, $MOVEFILE_DELAY_UNTIL_REBOOT)
if (-not $replaceOk) {
    throw "Failed to schedule pending DLL replacement. Win32Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
"$(Get-Date -Format o) Scheduled pending DLL rename" | Out-File -LiteralPath $logPath -Append -Encoding UTF8

$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceDll).Hash
"$(Get-Date -Format o) Scheduled reboot replacement. SourceHash=$sourceHash" | Out-File -LiteralPath $logPath -Append -Encoding UTF8
