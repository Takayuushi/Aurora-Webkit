$ErrorActionPreference = "Stop"
$buildDir = Join-Path $env:WEBKIT_DIR "WebKitBuild\Release"
$manifest = "build.ninja"
if (Test-Path (Join-Path $buildDir "build-Release.ninja")) { $manifest = "build-Release.ninja" }
$ninja = "C:\Program Files\Ninja\ninja.exe"
if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
if ($env:AURORA_CACHE_INCREMENTAL_COMPATIBLE -ne "true") { throw "Saved compiler/CMake cache metadata differs; refusing timestamp reconciliation." }
if ((git -C $env:WEBKIT_DIR rev-parse HEAD).Trim() -ne $env:WEBKIT_SHA) { throw "Restored WebKit SHA mismatch." }

$plan = @(& $ninja -C $buildDir -f $manifest -n Aurora 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Ninja dry-run failed." }
$compile = @($plan | Where-Object {
  $_.ToString() -match '^\s*\[\d+/\d+\]\s+Building\s+(?:C|CXX|ASM|OBJCXX)\s+object\s+Source[\\/](?:WebCore|JavaScriptCore|WTF|WebKit)[\\/]'
})
if ($compile.Count -eq 0) { Write-Host "No stale WebKit objects; no toolchain timestamp changes required."; exit 0 }

$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsInstall) { throw "MSVC installation could not be resolved." }
$msvcRoot = Join-Path $vsInstall ("VC\Tools\MSVC\" + $env:CURRENT_MSVC_VERSION)
$allowedRoots = @(
  (Join-Path $msvcRoot "include"),
  (Join-Path $msvcRoot "atlmfc\include"),
  (Join-Path (Join-Path $programFilesX86 "Windows Kits\10\Include") $env:CURRENT_SDK_VERSION)
)
foreach ($root in $allowedRoots) { if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Versioned include directory missing: $root" } }

$objects = @{}
foreach ($entry in $compile) {
  $parts = $entry.ToString() -split '\s+object\s+', 2
  if ($parts.Count -ne 2) { throw "Could not map stale WebKit object action: $entry" }
  $obj = $parts[1].Trim().Replace('/', [char]92)
  if (-not (Test-Path -LiteralPath (Join-Path $buildDir $obj) -PathType Leaf)) { throw "Cached object missing: $obj" }
  $objects[$obj.ToLowerInvariant()] = $obj
}

$outFile = Join-Path $env:RUNNER_TEMP "aurora-ninja-explain.out"
$errFile = Join-Path $env:RUNNER_TEMP "aurora-ninja-explain.err"
Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
$args = '-C "' + $buildDir + '" -f "' + $manifest + '" -d explain -n Aurora'
$proc = Start-Process -FilePath $ninja -ArgumentList $args -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
if ($proc.ExitCode -ne 0) { throw "Ninja explain failed." }
$lines = @()
if (Test-Path $outFile) { $lines += @(Get-Content -LiteralPath $outFile) }
if (Test-Path $errFile) { $lines += @(Get-Content -LiteralPath $errFile) }

$seen = @{}
$headerTimes = @{}
$unsafe = @()
foreach ($entry in $lines) {
  $line = $entry.ToString().Trim()
  $marker = $line.IndexOf("ninja explain: ", [StringComparison]::OrdinalIgnoreCase)
  if ($marker -ge 0) { $line = $line.Substring($marker + 15) }
  if ($line -notmatch '^output (?<output>.+) older than most recent input (?<input>.+) \((?<outputTicks>\d+) vs (?<inputTicks>\d+)\)
  $obj = $Matches["output"].Trim().Replace('/', [char]92)
  $key = $obj.ToLowerInvariant()
  if (-not $objects.ContainsKey($key)) { continue }
  $seen[$key] = $true
  $inputPath = [System.IO.Path]::GetFullPath($Matches["input"].Trim())
  $allowed = $false
  foreach ($root in $allowedRoots) {
    $prefix = [System.IO.Path]::GetFullPath($root).TrimEnd([char]92) + [char]92
    if ($inputPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $allowed = $true; break }
  }
  if (-not $allowed -or -not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { $unsafe += "$obj <= $inputPath"; continue }
  # Use Ninja's own FILETIME-derived output timestamp, not a separately sampled
  # filesystem timestamp. This keeps the normalization in the same clock domain
  # as Ninja's "older than" comparison.
  $ninjaFileTimeOffset = [long]126227704000000000
  $oneSecond = [long]10000000
  $safeTime = [DateTime]::FromFileTimeUtc(([long]$Matches["outputTicks"] + $ninjaFileTimeOffset - $oneSecond))
  if (-not $headerTimes.ContainsKey($inputPath) -or $safeTime -lt $headerTimes[$inputPath]) { $headerTimes[$inputPath] = $safeTime }
}
$missing = @($objects.Keys | Where-Object { -not $seen.ContainsKey($_) })
if ($unsafe.Count -gt 0) {
  $unsafe | ForEach-Object { Write-Host "Non-toolchain dependency: $_" }
  throw "At least one stale WebKit object depends on an input outside the version-pinned MSVC/SDK include roots. Refusing timestamp reconciliation."
}
if ($missing.Count -gt 0) {
  # Ninja reports downstream objects as merely "dirty" when their generated
  # inputs are also dirty; they have no direct "older than most recent input"
  # record. The direct timestamp causes have been mapped above. Reconcile only
  # those verified compiler headers, then let the mandatory dry-run below decide
  # whether the generated dependency chain is clean. Never waive that check.
  Write-Host "$($missing.Count) downstream WebKit object(s) have no direct timestamp explanation; they will be rechecked after safe header reconciliation."
  foreach ($key in $missing) { Write-Host "Deferred to post-reconciliation Ninja check: $($objects[$key])" }
}
if ($headerTimes.Count -eq 0) { throw "No compiler/SDK headers require timestamp reconciliation; refusing to proceed with unexplained WebKit compile actions." }
foreach ($item in $headerTimes.GetEnumerator()) {
  $before = [System.IO.File]::GetLastWriteTimeUtc($item.Key)
  [System.IO.File]::SetLastWriteTimeUtc($item.Key, $item.Value)
  $after = [System.IO.File]::GetLastWriteTimeUtc($item.Key)
  Write-Host "Header timestamp: $($item.Key)"
  Write-Host "  before UTC: $($before.ToString('o'))"
  Write-Host "  target UTC: $($item.Value.ToString('o'))"
  Write-Host "  after UTC:  $($after.ToString('o'))"
  if ($after -gt $item.Value.AddMilliseconds(250)) { throw "Header timestamp update did not persist: $($item.Key)" }
}
Write-Host "Reconciled $($headerTimes.Count) versioned MSVC/SDK header timestamps only; no object files or .ninja_deps records were modified."

$verify = @(& $ninja -C $buildDir -f $manifest -n Aurora 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Ninja verification failed after header reconciliation." }
$remaining = @($verify | Where-Object {
  $_.ToString() -match '^\s*\[\d+/\d+\]\s+Building\s+(?:C|CXX|ASM|OBJCXX)\s+object\s+Source[\\/](?:WebCore|JavaScriptCore|WTF|WebKit)[\\/]'
})
if ($remaining.Count -gt 0) {
  Write-Host "WebKit compile actions remain after timestamp reconciliation:"
  $remaining | Select-Object -First 16 | ForEach-Object { Write-Host $_ }
  $outFile = Join-Path $env:RUNNER_TEMP "aurora-ninja-post-explain.out"
  $errFile = Join-Path $env:RUNNER_TEMP "aurora-ninja-post-explain.err"
  Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
  $args = '-C "' + $buildDir + '" -f "' + $manifest + '" -d explain -n Aurora'
  $proc = Start-Process -FilePath $ninja -ArgumentList $args -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
  $details = @()
  if (Test-Path $outFile) { $details += @(Get-Content -LiteralPath $outFile) }
  if (Test-Path $errFile) { $details += @(Get-Content -LiteralPath $errFile) }
  $details | Where-Object { $_.ToString() -match '(?i)(older than most recent input| is dirty$)' } | Select-Object -First 40 | ForEach-Object { Write-Host $_.ToString() }
  throw "WebKit compile actions remain. Refusing implicit rebuild."
}
) { continue }
  $obj = $Matches["output"].Trim().Replace('/', [char]92)
  $key = $obj.ToLowerInvariant()
  if (-not $objects.ContainsKey($key)) { continue }
  $seen[$key] = $true
  $inputPath = [System.IO.Path]::GetFullPath($Matches["input"].Trim())
  $allowed = $false
  foreach ($root in $allowedRoots) {
    $prefix = [System.IO.Path]::GetFullPath($root).TrimEnd([char]92) + [char]92
    if ($inputPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $allowed = $true; break }
  }
  if (-not $allowed -or -not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { $unsafe += "$obj <= $inputPath"; continue }
  $objPath = Join-Path $buildDir $obj
  $safeTime = [System.IO.File]::GetLastWriteTimeUtc($objPath).AddSeconds(-1)
  if (-not $headerTimes.ContainsKey($inputPath) -or $safeTime -lt $headerTimes[$inputPath]) { $headerTimes[$inputPath] = $safeTime }
}
$missing = @($objects.Keys | Where-Object { -not $seen.ContainsKey($_) })
if ($unsafe.Count -gt 0) {
  $unsafe | ForEach-Object { Write-Host "Non-toolchain dependency: $_" }
  throw "At least one stale WebKit object depends on an input outside the version-pinned MSVC/SDK include roots. Refusing timestamp reconciliation."
}
if ($missing.Count -gt 0) {
  # Ninja reports downstream objects as merely "dirty" when their generated
  # inputs are also dirty; they have no direct "older than most recent input"
  # record. The direct timestamp causes have been mapped above. Reconcile only
  # those verified compiler headers, then let the mandatory dry-run below decide
  # whether the generated dependency chain is clean. Never waive that check.
  Write-Host "$($missing.Count) downstream WebKit object(s) have no direct timestamp explanation; they will be rechecked after safe header reconciliation."
  foreach ($key in $missing) { Write-Host "Deferred to post-reconciliation Ninja check: $($objects[$key])" }
}
if ($headerTimes.Count -eq 0) { throw "No compiler/SDK headers require timestamp reconciliation; refusing to proceed with unexplained WebKit compile actions." }
foreach ($item in $headerTimes.GetEnumerator()) { [System.IO.File]::SetLastWriteTimeUtc($item.Key, $item.Value) }
Write-Host "Reconciled $($headerTimes.Count) versioned MSVC/SDK header timestamps only; no object files or .ninja_deps records were modified."

$verify = @(& $ninja -C $buildDir -f $manifest -n Aurora 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Ninja verification failed after header reconciliation." }
$remaining = @($verify | Where-Object {
  $_.ToString() -match '^\s*\[\d+/\d+\]\s+Building\s+(?:C|CXX|ASM|OBJCXX)\s+object\s+Source[\\/](?:WebCore|JavaScriptCore|WTF|WebKit)[\\/]'
})
if ($remaining.Count -gt 0) { $remaining | Select-Object -First 16 | ForEach-Object { Write-Host $_ }; throw "WebKit compile actions remain. Refusing implicit rebuild." }
