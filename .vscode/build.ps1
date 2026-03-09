# Keil Build Script
param(
    [string]$UV4Path,
    [string]$ProjectFile,
    [string]$Target,
    [string]$Mode = "build"
)

Write-Host "Starting compilation..." -ForegroundColor Cyan

# Execute compilation and wait for completion
if ($Mode -eq "debug") {
    $process = Start-Process -FilePath $UV4Path -ArgumentList "-d", $ProjectFile, "-j4", "-t", $Target -PassThru -NoNewWindow
} else {
    $process = Start-Process -FilePath $UV4Path -ArgumentList "-b", $ProjectFile, "-j4", "-t", $Target -PassThru -NoNewWindow
}

# Wait for the process to complete
Write-Host "Compiling... (PID: $($process.Id))" -ForegroundColor Gray
$process.WaitForExit()
Write-Host "Compilation process completed with exit code: $($process.ExitCode)" -ForegroundColor Gray

# Wait a moment for file system to sync
Start-Sleep -Milliseconds 500

# Read and display log
$logFile = Join-Path (Split-Path $ProjectFile) "Out_File\TraceVector.build_log.htm"

if (Test-Path $logFile) {
    Write-Host "`n========== Build Result ==========" -ForegroundColor Cyan
    
    # Read with Windows-1252 encoding
    $content = [System.IO.File]::ReadAllText($logFile, [System.Text.Encoding]::GetEncoding(1252))
    # Remove HTML tags
    $content = $content -replace '<[^>]+>', ''
    $content = $content -replace '&nbsp;', ' '
    
    # Process line by line
    $lines = $content -split "`r?`n" | Where-Object { $_.Trim() -ne '' }
    
    foreach ($line in $lines) {
        if ($line -match 'Error' -and $line -notmatch '0 Error') {
            Write-Host $line -ForegroundColor Red
        }
        elseif ($line -match 'Warning' -and $line -notmatch '0 Warning') {
            Write-Host $line -ForegroundColor Yellow
        }
        elseif ($line -match 'Program Size|Build Time|Error\(s\)|Warning\(s\)') {
            Write-Host $line -ForegroundColor Green
        }
        elseif ($line -match 'compiling|assembling|linking') {
            Write-Host $line -ForegroundColor Gray
        }
        else {
            Write-Host $line
        }
    }
    
    Write-Host "==================================`n" -ForegroundColor Cyan
} else {
    Write-Host "Build completed, but log file not found" -ForegroundColor Yellow
}

