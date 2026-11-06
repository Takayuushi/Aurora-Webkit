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
$compilePattern = '^\s*\[\d+/\d+\]\s+Building\s+(?:C|CXX|ASM|OBJCXX)\s+object\s+Source[\\/](?:WebCore|JavaScriptCore|WTF|WebKit)[\\/]'
$compile = @($plan | Where-Object { $_.ToString() -match $compilePattern })
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
foreach ($root in $allowedRoots) {
  if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Versioned include directory missing: $root" }
}

$objects = @{}
foreach ($entry in $compile) {
  $parts = $entry.ToString() -split '\s+object\s+', 2
  if ($parts.Count -ne 2) { throw "Could not map stale WebKit object action: $entry" }
  $object = $parts[1].Trim().Replace('/', [char]92)
  if (-not (Test-Path -LiteralPath (Join-Path $buildDir $object) -PathType Leaf)) { throw "Cached object missing: $object" }
  $objects[$object.ToLowerInvariant()] = $object
}

$outFile = Join-Path $env:RUNNER_TEMP "aurora-ninja-explain.out"
$errFile = Join-Path $env:RUNNER_TEMP "aurora-ninja-explain.err"
Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
$explainArgs = '-C "' + $buildDir + '" -f "' + $manifest + '" -d explain -n Aurora'
$process = Start-Process -FilePath $ninja -ArgumentList $explainArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
if ($process.ExitCode -ne 0) { throw "Ninja explain failed with exit code $($process.ExitCode)." }
$lines = @()
if (Test-Path -LiteralPath $outFile) { $lines += @(Get-Content -LiteralPath $outFile) }
if (Test-Path -LiteralPath $errFile) { $lines += @(Get-Content -LiteralPath $errFile) }

$headerTimes = @{}
$unsafe = @()
$matched = 0
$explainPattern = '^output (?<output>.+) older than most recent input (?<input>.+) \((?<outputTicks>\d+) vs (?<inputTicks>\d+)\)$'
$ninjaFileTimeOffset = [long]126227704000000000
$oneSecond = [long]10000000

foreach ($entry in $lines) {
  $line = $entry.ToString().Trim()
  $marker = $line.IndexOf("ninja explain: ", [StringComparison]::OrdinalIgnoreCase)
  if ($marker -ge 0) { $line = $line.Substring($marker + 15) }
  if ($line -notmatch $explainPattern) { continue }

  $object = $Matches["output"].Trim().Replace('/', [char]92)
  $objectKey = $object.ToLowerInvariant()
  if (-not $objects.ContainsKey($objectKey)) { continue }
  $matched++

  $inputPath = [System.IO.Path]::GetFullPath($Matches["input"].Trim())
  $allowed = $false
  foreach ($root in $allowedRoots) {
    $prefix = [System.IO.Path]::GetFullPath($root).TrimEnd([char]92) + [char]92
    if ($inputPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $allowed = $true; break }
  }
  if (-not $allowed -or -not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
    $unsafe += "$object <= $inputPath"
    continue
  }

  # Normalize in the same FILETIME clock domain printed by Ninja.
  $safeFileTime = [long]$Matches["outputTicks"] + $ninjaFileTimeOffset - $oneSecond
  $safeTime = [DateTime]::FromFileTimeUtc($safeFileTime)
  if (-not $headerTimes.ContainsKey($inputPath) -or $safeTime -lt $headerTimes[$inputPath]) {
    $headerTimes[$inputPath] = $safeTime
  }
}

if ($unsafe.Count -gt 0) {
  $unsafe | Select-Object -First 20 | ForEach-Object { Write-Host "Non-toolchain dependency: $_" }
  throw "Ninja reports stale WebKit objects with dependencies outside the version-pinned MSVC/SDK include roots. Refusing timestamp changes."
}
if ($headerTimes.Count -eq 0) {
  Write-Host "Ninja planned $($compile.Count) WebKit object compile actions but exposed no directly explainable version-pinned header dependency."
  $lines | Select-Object -First 40 | ForEach-Object { Write-Host $_.ToString() }
  throw "No safe toolchain-header timestamp correction is available."
}

Write-Host "Mapped $matched direct object/header timestamp cause(s) to $($headerTimes.Count) unique version-pinned header(s)."
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
Write-Host "No object files or .ninja_deps records were modified."

$verify = @(& $ninja -C $buildDir -f $manifest -n Aurora 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Ninja verification failed after header reconciliation." }
$remaining = @($verify | Where-Object { $_.ToString() -match $compilePattern })
if ($remaining.Count -gt 0) {
  Write-Host "WebKit compile actions remain after timestamp reconciliation:"
  $remaining | Select-Object -First 16 | ForEach-Object { Write-Host $_.ToString() }
  $postOut = Join-Path $env:RUNNER_TEMP "aurora-ninja-post-explain.out"
  $postErr = Join-Path $env:RUNNER_TEMP "aurora-ninja-post-explain.err"
  Remove-Item -LiteralPath $postOut, $postErr -Force -ErrorAction SilentlyContinue
  $postProcess = Start-Process -FilePath $ninja -ArgumentList $explainArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $postOut -RedirectStandardError $postErr
  $postLines = @()
  if (Test-Path -LiteralPath $postOut) { $postLines += @(Get-Content -LiteralPath $postOut) }
  if (Test-Path -LiteralPath $postErr) { $postLines += @(Get-Content -LiteralPath $postErr) }
  Write-Host "Remaining direct Ninja dependency explanations:"
  $postLines | Where-Object { $_.ToString() -match '(?i)(older than most recent input| is dirty$)' } | Select-Object -First 40 | ForEach-Object { Write-Host $_.ToString() }
  throw "WebKit compile actions remain. Refusing implicit rebuild."
}
Write-Host "Ninja dry-run passed after safe toolchain-header timestamp reconciliation."
