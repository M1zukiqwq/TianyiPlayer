# Copy every runtime DLL the executable transitively needs into its folder.
# MSYS2/MinGW libraries have version-suffixed names and deep dependency trees
# (FFmpeg alone pulls in 50+ codecs), so resolve the closure at build time.
param(
    [Parameter(Mandatory = $true)][string]$TargetDir,
    [Parameter(Mandatory = $true)][string]$ToolchainBin,
    [Parameter(Mandatory = $true)][string]$Objdump
)

$ErrorActionPreference = 'Stop'
$target = (Resolve-Path $TargetDir).Path
$toolchain = (Resolve-Path $ToolchainBin).Path

$system = @{}
foreach ($d in (Get-ChildItem "$env:windir\System32" -Filter '*.dll' -ErrorAction SilentlyContinue)) {
    $system[$d.Name.ToLower()] = $true
}

$queue = [System.Collections.Queue]::new()
Get-ChildItem $target -Include '*.exe', '*.dll' -Recurse | ForEach-Object { $queue.Enqueue($_.FullName) }

$resolved = @{}
while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    if ($resolved.ContainsKey($file)) { continue }
    $resolved[$file] = $true

    $deps = & $Objdump -p $file 2>$null |
        Select-String 'DLL Name:' |
        ForEach-Object { ($_ -split 'DLL Name:\s+')[1].Trim() }

    foreach ($dep in $deps) {
        $lower = $dep.ToLower()
        if ($lower -like 'api-ms-win-*' -or $lower -like 'ext-ms-win-*') { continue }
        if ($system.ContainsKey($lower)) { continue }

        $localPath = Join-Path $target $dep
        if (Test-Path $localPath) {
            $queue.Enqueue($localPath)
            continue
        }

        $toolchainPath = Join-Path $toolchain $dep
        if (Test-Path $toolchainPath) {
            Copy-Item $toolchainPath $target -Force
            Write-Host "  copied $dep"
            $queue.Enqueue($toolchainPath)
        } else {
            Write-Warning "unresolved dependency: $dep (needed by $(Split-Path $file -Leaf))"
        }
    }
}
Write-Host "runtime dependency copy complete -> $target"
