$ErrorActionPreference = "Stop"
$buildDir = Join-Path $env:WEBKIT_DIR "WebKitBuild\Release"
$manifest = "build.ninja"
if (Test-Path (Join-Path $buildDir "build-Release.ninja")) { $manifest = "build-Release.ninja" }
$ninja = "C:\Program Files\Ninja\ninja.exe"
if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
if ($env:AURORA_CACHE_INCREMENTAL_COMPATIBLE -ne "true") { throw "Saved compiler/CMake cache metadata differs; refusing timestamp reconciliation." }
if ((git -C $env:WEBKIT_DIR rev-parse HEAD).Trim() -ne $env:WEBKIT_SHA) { throw "Restored WebKit SHA mismatch." }

$compilePattern = '^\s*\[\d+/\d+\]\s+Building\s+(?:C|CXX|ASM|OBJCXX)\s+object\s+Source[\\/]'
$plan = @(& $ninja -C $buildDir -f $manifest -n Aurora 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Ninja dry-run failed." }
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

# Confirm the cached objects exist and establish a timestamp earlier than every
# object Ninja currently wants to rebuild. The cache fingerprint and WebKit SHA
# were checked above, so these are the exact headers used by the cached build.
$objects = @()
foreach ($entry in $compile) {
  $parts = $entry.ToString() -split '\s+object\s+', 2
  if ($parts.Count -ne 2) { throw "Could not map stale WebKit object action: $entry" }
  $object = $parts[1].Trim().Replace('/', [char]92)
  $objectPath = Join-Path $buildDir $object
  if (-not (Test-Path -LiteralPath $objectPath -PathType Leaf)) { throw "Cached object missing: $object" }
  $objects += (Get-Item -LiteralPath $objectPath).LastWriteTimeUtc
}
$oldestObject = ($objects | Sort-Object | Select-Object -First 1)
$targetTime = $oldestObject.AddSeconds(-2)
Write-Host "Ninja currently plans $($compile.Count) WebKit object compile actions."
Write-Host "Normalizing only the exact fingerprint-matched MSVC/SDK include roots to before the oldest affected cached object: $($targetTime.ToString('o'))"

$changed = 0
$scanned = 0
foreach ($root in $allowedRoots) {
  Get-ChildItem -LiteralPath $root -File -Recurse -Force | ForEach-Object {
    $scanned++
    $current = $_.LastWriteTimeUtc
    if ($current -gt $targetTime) {
      [System.IO.File]::SetLastWriteTimeUtc($_.FullName, $targetTime)
      $after = [System.IO.File]::GetLastWriteTimeUtc($_.FullName)
      if ($after -gt $targetTime.AddMilliseconds(250)) { throw "Header timestamp update did not persist: $($_.FullName)" }
      $changed++
    }
  }
}
Write-Host "Scanned $scanned version-pinned toolchain headers; adjusted $changed timestamps. No object files or .ninja_deps records were modified."

$verify = @(& $ninja -C $buildDir -f $manifest -n Aurora 2>&1)
if ($LASTEXITCODE -ne 0) { throw "Ninja verification dry-run failed after safe header timestamp normalization." }
$remaining = @($verify | Where-Object { $_.ToString() -match $compilePattern })
if ($remaining.Count -gt 0) {
  Write-Host "Remaining WebKit compile actions after toolchain-header normalization:"
  $remaining | Select-Object -First 30 | ForEach-Object { Write-Host $_.ToString() }

  # Capture Ninja's dependency explanations rather than assuming every remaining
  # action is still caused by a version-pinned MSVC/SDK header.
  $previousErrorActionPreference = $ErrorActionPreference
  try {
    # Ninja writes dependency explanations to stderr. Windows PowerShell 5.1
    # promotes native stderr to NativeCommandError when ErrorActionPreference=Stop.
    # Temporarily continue so the diagnostic output is captured without aborting.
    $ErrorActionPreference = "Continue"
    $explain = @(& $ninja -C $buildDir -f $manifest -d explain -n Aurora 2>&1)
    $explainExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  if ($explainExitCode -ne 0) {
    Write-Host ($explain -join [Environment]::NewLine)
    throw "Ninja explanation pass failed while diagnosing remaining WebKit compile actions (exit $explainExitCode)."
  }
  Write-Host "Ninja dependency explanations for the remaining compile plan:"
  $explain | Where-Object {
    $_.ToString() -match '(?i)explain:|older than most recent input|recorded mtime|missing|no known input'
  } | Select-Object -First 160 | ForEach-Object { Write-Host $_.ToString() }

  throw "Ninja still reports $($remaining.Count) WebKit source compile actions after version-pinned header timestamp normalization. Refusing implicit rebuild."
}
Write-Host "Ninja dry-run passed: no WebKit source compile actions remain. Proceeding with Aurora-only incremental build."
