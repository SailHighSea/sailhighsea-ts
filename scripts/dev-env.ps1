# Dev environment for the Tauri port. Tools live in D:\apps (nothing is installed system-wide).
# Usage (PowerShell):  . .\scripts\dev-env.ps1
$env:RUSTUP_HOME = 'D:\apps\rust\rustup'
$env:CARGO_HOME  = 'D:\apps\rust\cargo'
$env:PATH = 'D:\apps\rust\cargo\bin;D:\apps\node;' + $env:PATH
$vc = 'D:\apps\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
cmd /c "`"$vc`" >nul 2>nul && set" 2>$null | ForEach-Object {
    if ($_ -match '^(INCLUDE|LIB|LIBPATH|PATH|WindowsSdkDir|WindowsSDKVersion|VCINSTALLDIR)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$ErrorActionPreference = $prev
