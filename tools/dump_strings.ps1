param([string]$Version = 'v196', [string]$Filter = '')

$dir = 'c:\Users\Hugo\Desktop\csprotoolinternal\tools'
$path = Join-Path $dir "CSProTool_$Version.dll"
if (-not (Test-Path $path)) { Write-Output "$Version : ABSENT"; exit 1 }

$bytes = [System.IO.File]::ReadAllBytes($path)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)

$matches = [regex]::Matches($ascii, '[\x20-\x7E]{6,200}')
$out = foreach ($m in $matches) {
    $s = $m.Value
    if ($Filter -and $s -notmatch $Filter) { continue }
    $s
}
$out | Select-Object -Unique
