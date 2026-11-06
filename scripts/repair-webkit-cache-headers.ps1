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
  if ($line -notmatch '^output (?<output>.+) older than most recent input (?<input>.+) \(\d+ vs \d+\)$') { continue }
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
if ($missing.Count -gt 0 -or $unsafe.Count -gt 0) {
  foreach ($key in $missing) {
    $obj = $objects[$key]
    $leaf = [System.IO.Path]::GetFileName($obj)
    Write-Host "Unexplained object: $obj"
    $positions = @()
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
      if ($lines[$lineIndex].ToString().IndexOf($leaf, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        $positions += $lineIndex
      }
    }
    if ($positions.Count -eq 0) {
      Write-Host "No filename-specific Ninja explain record was captured for $leaf."
    } else {
      foreach ($position in $positions) {
        $contextStart = [Math]::Max(0, $position - 30)
        $contextEnd = [Math]::Min($lines.Count - 1, $position + 8)
        Write-Host "Ninja explain context for $leaf (log lines $($contextStart + 1)-$($contextEnd + 1)):"
        for ($contextIndex = $contextStart; $contextIndex -le $contextEnd; $contextIndex++) {
          Write-Host ("Ninja trace: " + $lines[$contextIndex].ToString())
        }
      }
    }
  }
  $unsafe | ForEach-Object { Write-Host "Non-toolchain dependency: $_" }
  throw "Stale WebKit objects are not explained solely by version-pinned MSVC/SDK headers."
}
if ($headerTimes.Count -eq 0) { throw "No compiler/SDK headers require timestamp reconciliation." }
foreach ($item in $headerTimes.GetEnumerator()) { [System.IO.File]::SetLastWriteTimeUtc($item.Key, $item.Value) }
Write-Host "Reconciled $($headerTimes.Count) versioned MSVC/SDK header timestamps only; no object files or .ninja_deps records were modified."

$verify = @(& $ninja -C $buildDir -f $manifest -n Aurora 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Ninja verification failed after header reconciliation." }
$remaining = @($verify | Where-Object {
  $_.ToString() -match '^\s*\[\d+/\d+\]\s+Building\s+(?:C|CXX|ASM|OBJCXX)\s+object\s+Source[\\/](?:WebCore|JavaScriptCore|WTF|WebKit)[\\/]'
})
if ($remaining.Count -gt 0) { $remaining | Select-Object -First 16 | ForEach-Object { Write-Host $_ }; throw "WebKit compile actions remain. Refusing implicit rebuild." }
