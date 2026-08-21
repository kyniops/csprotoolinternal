param([string[]]$Versions = @('v196','v212','v220','v221'))

$dir = 'c:\Users\Hugo\Desktop\csprotoolinternal\tools'
$patterns = @('UpdateSkybox', '\[atm\]', 'env_sky', 'gradient_fog', 'tonemap', 'player_visibility', 'post_processing')

foreach ($v in $Versions) {
    $path = Join-Path $dir "CSProTool_$v.dll"
    if (-not (Test-Path $path)) { Write-Output "$v : ABSENT"; continue }

    $bytes = [System.IO.File]::ReadAllBytes($path)
    $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)

    $found = @()
    foreach ($p in $patterns) {
        if ([regex]::IsMatch($ascii, $p)) { $found += $p }
    }
    Write-Output "$v : $($found -join ' | ')"
}
