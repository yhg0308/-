<#
flash-app.ps1 - reliable flash for this ESP32 project (any folder, any project name).

WHY THIS EXISTS
---------------
The erase used by esptool's stub/ROM does not take effect for the FINAL 64KB block
of the requested range. NOR flash programming can only turn 1 bits into 0, so
writing the app on top of un-erased cells yields (new AND old), and esptool aborts:

    A fatal error occurred: MD5 of file does not match data in flash!

Measured on this board, all of it reproducible:
    erase_region 0x10000 0x33000   -> erased 0x10000..0x40000, tail block untouched
    erase_region 0x40000 0x10000   -> erased nothing at all ("successful")
    erase_region 0x40000 0x20000   -> erased the first block only
The tail of the app image lived at 0x40000..0x42280, i.e. exactly in the skipped
block, so `idf.py flash` failed on the same 6845 bytes every single time.

WHAT THIS SCRIPT DOES
---------------------
  1. reads build/flash_args, so it follows whatever this project actually builds
     (bin names, offsets, flash flags) - nothing is hardcoded;
  2. erases one contiguous span covering every image, PLUS one extra 64KB of slack
     so the "sacrificed" last block falls beyond the images;
  3. READS BACK and proves the erase really took (falls back to a full chip erase);
  4. writes the images;
  5. reads everything back and byte-compares against the build.

usage:
  powershell -NoProfile -ExecutionPolicy Bypass -File flash-app.ps1
  ... -Port COM7 -Baud 460800        # defaults
  ... -SkipBootBlock                 # keep nvs/phy_init (only if bootloader/partition table are unchanged)
  ... -FullChipErase                 # slow (~42s), most thorough
  ... -Force                         # flash even if sources are newer than the build
#>
param(
    [string]$Port = 'COM7',
    [int]$Baud = 460800,
    [switch]$FullChipErase,
    [switch]$SkipBootBlock,
    [switch]$Force
)

$ErrorActionPreference = 'Continue'
$PROJ  = $PSScriptRoot
$build = Join-Path $PROJ 'build'
$BLOCK = 0x10000      # 64KB - the only granularity esptool's erase really handles
$tmp   = Join-Path $env:TEMP 'esp_flash_tmp'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# ------------------------------------------------------------- tool locations --
$py  = 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$esp = 'D:\Espressif\frameworks\esp-idf-v5.5.5\components\esptool_py\esptool\esptool.py'
if (-not (Test-Path $esp)) {
    $alt = Join-Path $env:IDF_PATH 'components\esptool_py\esptool\esptool.py'
    if (Test-Path $alt) { $esp = $alt } else { Write-Host "!! esptool.py not found (tried the Espressif install and `$env:IDF_PATH)"; exit 1 }
}
if (-not (Test-Path $py)) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { $py = $cmd.Source } else { Write-Host "!! python not found"; exit 1 }
}

# -------------------------------------------------------------- build inputs --
$descFile = Join-Path $build 'project_description.json'
$argsFile = Join-Path $build 'flash_args'
if (-not (Test-Path $argsFile)) { Write-Host "!! $argsFile not found - run 'idf.py build' in $PROJ first"; exit 1 }
$desc = $null
if (Test-Path $descFile) { try { $desc = Get-Content $descFile -Raw | ConvertFrom-Json } catch { } }

# target chip
$Chip = 'esp32s3'
if ($desc -and $desc.target) { $Chip = $desc.target }

# a build/ copied from another folder carries another project's binaries
if ($desc -and $desc.build_dir) {
    $was = [IO.Path]::GetFullPath($desc.build_dir).TrimEnd('\')
    $now = [IO.Path]::GetFullPath($build).TrimEnd('\')
    if ($was -ne $now) {
        Write-Host "!! this build/ was produced in a different folder:"
        Write-Host "     built in : $was"
        Write-Host "     now here : $now"
        Write-Host "   The .bin files are from that other folder (or are stale). Do this first:"
        Write-Host "     idf.py fullclean      (or just delete the build folder)"
        Write-Host "     idf.py build"
        exit 1
    }
}

# ---------------------------------------------------------------- flash_args --
$lines = Get-Content $argsFile | Where-Object { $_.Trim() -ne '' }
$flagLine = ($lines | Where-Object { $_.TrimStart().StartsWith('--') } | Select-Object -First 1)
if (-not $flagLine) { Write-Host "!! $argsFile has no flash-flag line"; exit 1 }
$flagLine = $flagLine.Trim()
$flags = $flagLine -split '\s+'

$entries = @()
foreach ($l in ($lines | Where-Object { -not $_.TrimStart().StartsWith('--') })) {
    $p = $l.Trim() -split '\s+', 2
    if ($p.Count -ne 2) { continue }
    $path = Join-Path $build $p[1]
    if (-not (Test-Path $path)) { Write-Host "!! missing image $path - rebuild"; exit 1 }
    $entries += , @{ off = [Convert]::ToInt32($p[0], 16); path = $path; len = (Get-Item $path).Length }
}
if ($entries.Count -eq 0) { Write-Host "!! could not parse $argsFile"; exit 1 }

# which entry is the application? look it up in the partition table.
$appOff = $null
$csvName = $null
$sdk = Join-Path $PROJ 'sdkconfig'
if (Test-Path $sdk) {
    $m = Select-String -Path $sdk -Pattern '^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="(.+)"' | Select-Object -First 1
    if ($m) { $csvName = $m.Matches[0].Groups[1].Value }
}
if (-not $csvName) { $csvName = (Get-ChildItem $PROJ -Filter 'partitions*.csv' -File | Select-Object -First 1).Name }
if ($csvName -and (Test-Path (Join-Path $PROJ $csvName))) {
    foreach ($row in (Get-Content (Join-Path $PROJ $csvName))) {
        if ($row.TrimStart().StartsWith('#')) { continue }
        $c = $row.Split(',')
        if ($c.Count -ge 4 -and $c[1].Trim() -eq 'app') {
            $appOff = [Convert]::ToInt32($c[3].Trim(), 16); break
        }
    }
}
$app = $null
if ($null -ne $appOff) { $app = $entries | Where-Object { $_.off -eq $appOff } | Select-Object -First 1 }
if (-not $app) { $app = $entries | Sort-Object { -$_.len } | Select-Object -First 1 }

# ------------------------------------------------- stale sources vs artifacts --
if (-not $Force) {
    $bins = @($entries | ForEach-Object { Get-Item $_.path })
    $oldestBin = ($bins | Sort-Object LastWriteTime | Select-Object -First 1).LastWriteTime
    $src = Get-ChildItem $PROJ -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Extension -in @('.c', '.h', '.cpp', '.hpp', '.cxx') -or $_.Name -eq 'CMakeLists.txt' -or $_.Name -eq 'sdkconfig' -or $_.Extension -eq '.csv'
        } |
        Where-Object { $_.FullName -notlike "$build\*" -and $_.FullName -notmatch '\\\.' }
    if ($src) {
        $newest = ($src | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
        if ($newest.LastWriteTime -gt $oldestBin) {
            Write-Host "!! sources are newer than the built images - you would flash old firmware:"
            Write-Host ("     {0}  ({1:yyyy-MM-dd HH:mm:ss})" -f $newest.FullName, $newest.LastWriteTime)
            Write-Host ("     oldest image {0:yyyy-MM-dd HH:mm:ss}" -f $oldestBin)
            Write-Host "   run 'idf.py build' first, or pass -Force to flash anyway."
            exit 1
        }
    }
}

# ------------------------------------------------------------------ the plan --
Write-Host "== plan =="
Write-Host ("   project     : {0}" -f $PROJ)
Write-Host ("   chip / port : {0} / {1} @ {2}" -f $Chip, $Port, $Baud)
Write-Host ("   flash flags : {0}" -f $flagLine)
foreach ($e in ($entries | Sort-Object { $_.off })) {
    $tag = if ($e.path -eq $app.path) { '  <- app' } else { '' }
    Write-Host ("   0x{0,-8:x} {1,-24} {2,8} bytes{3}" -f $e.off, (Split-Path $e.path -Leaf), $e.len, $tag)
}

function Esp([string[]]$a) { return (& $py $esp @a 2>&1 | Out-String) }
function Show([string]$out) {
    $out -split "`r?`n" |
        Select-String 'Fatal|rror|not a multiple|completed|Wrote|verified|md5|Hash of|SHA digest' |
        ForEach-Object { Write-Host ("      " + $_.Line.Trim()) }
}
function Read-Region([int]$off, [int]$len, [string]$name) {
    $f = Join-Path $tmp "$name.bin"
    if (Test-Path $f) { Remove-Item $f -Force }
    Esp @('-p', $Port, '-b', "$Baud", '--chip', $Chip, '--after', 'no_reset',
          'read_flash', ("0x{0:x}" -f $off), "$len", $f) | Out-Null
    if (-not (Test-Path $f)) { return $null }
    return [System.IO.File]::ReadAllBytes($f)
}
function Test-Erased([int]$off, [int]$len) {
    $d = Read-Region $off $len "erased_$('{0:x}' -f $off)"
    if ($null -eq $d) { return @{ ok = $false; bad = -1; first = -1 } }
    $bad = 0; $first = -1
    for ($i = 0; $i -lt $d.Length; $i++) { if ($d[$i] -ne 0xFF) { $bad++; if ($first -lt 0) { $first = $i + $off } } }
    return @{ ok = ($bad -eq 0); bad = $bad; first = $first }
}
function Do-Erase([int]$off, [int]$size, [string]$what) {
    Write-Host ("   erase {0}: 0x{1:x} + 0x{2:x}" -f $what, $off, $size)
    Show (Esp @('-p', $Port, '-b', "$Baud", '--chip', $Chip, '--after', 'no_reset',
                'erase_region', ("0x{0:x}" -f $off), "$size"))
}
function Verify-Plan {
    $ok = $true
    foreach ($e in ($entries | Sort-Object { $_.off })) {
        if ($SkipBootBlock -and $e.path -ne $app.path) { continue }
        $r = Test-Erased $e.off $e.len
        $n = Split-Path $e.path -Leaf
        if ($r.ok) { Write-Host ("   {0,-24} 0x{1:x}..0x{2:x} is 0xFF" -f $n, $e.off, ($e.off + $e.len)) }
        else {
            Write-Host ("   {0,-24} !! still dirty: {1} bytes, first at 0x{2:x}" -f $n, $r.bad, $r.first)
            $ok = $false
        }
    }
    return $ok
}

# ------------------------------------------------------------------- erase ---
$targets = if ($SkipBootBlock) { @($app) } else { $entries }
$lo = ($targets | ForEach-Object { $_.off } | Measure-Object -Minimum).Minimum
$hi = ($targets | ForEach-Object { $_.off + $_.len } | Measure-Object -Maximum).Maximum
$loB = [int]([Math]::Floor($lo / $BLOCK) * $BLOCK)
$hiB = [int]([Math]::Ceiling($hi / $BLOCK) * $BLOCK)
$eraseEnd = $hiB + $BLOCK        # this extra block is the one the erase drops

Write-Host "== erase =="
if ($SkipBootBlock) { Write-Host "   (-SkipBootBlock: only the application span, nvs/phy_init untouched)" }
Do-Erase $loB ($eraseEnd - $loB) 'span'
$ok = Verify-Plan

if ((-not $ok) -or $FullChipErase) {
    Write-Host "== full chip erase (slow, wipes nvs) =="
    Show (Esp @('-p', $Port, '-b', "$Baud", '--chip', $Chip, '--after', 'no_reset', 'erase_flash'))
    $ok = Verify-Plan
}
if (-not $ok) {
    Write-Host "!! the region cannot be erased - flash chip problem. Not writing."
    exit 2
}

# ------------------------------------------------------------------- write ---
Write-Host "== write =="
$wa = @('-p', $Port, '-b', "$Baud", '--before', 'default_reset', '--after', 'no_reset', '--chip', $Chip, 'write_flash') + $flags
foreach ($e in ($entries | Sort-Object { $_.off })) { $wa += ("0x{0:x}" -f $e.off); $wa += $e.path }
Show (Esp $wa)

# ------------------------------------------------------------------ verify ---
Write-Host "== verify by reading the chip back =="
$allOk = $true
foreach ($e in ($entries | Sort-Object { $_.off })) {
    $want = [System.IO.File]::ReadAllBytes($e.path)
    $got = Read-Region $e.off $want.Length ("got_$('{0:x}' -f $e.off)")
    $name = Split-Path $e.path -Leaf
    if ($null -eq $got) { Write-Host ("   {0,-24} READ FAILED" -f $name); $allOk = $false; continue }
    $diff = 0; $first = -1; $subset = 0
    for ($i = 0; $i -lt $want.Length; $i++) {
        if ($got[$i] -ne $want[$i]) {
            $diff++; if ($first -lt 0) { $first = $i + $e.off }
            if (($got[$i] -band $want[$i]) -eq $got[$i]) { $subset++ }
        }
    }
    if ($diff -eq 0) { Write-Host ("   {0,-24} OK   {1} bytes identical" -f $name, $want.Length) }
    else {
        $allOk = $false
        Write-Host ("   {0,-24} BAD  {1} bytes differ, first at 0x{2:x} ({3} are bit-subsets of the file)" -f $name, $diff, $first, $subset)
        if ($SkipBootBlock -and $e.path -ne $app.path) { Write-Host "        (try again without -SkipBootBlock: this image's block was not erased)" }
    }
}

Write-Host "== reset =="
Esp @('-p', $Port, '-b', '115200', '--chip', $Chip, '--after', 'hard_reset', 'run') | Out-Null
if ($allOk) { Write-Host "FLASH OK - every region on the chip matches the build."; exit 0 }
Write-Host "FLASH VERIFY FAILED - see the BAD lines above."; exit 3
