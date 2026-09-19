# 逐字节串口监听
Out-File -FilePath "C:\Users\gzf\Desktop\open-vela\boot_log.txt" -Encoding utf8 -InputObject "=== 监听开始 ==="
Write-Host "=== COM4/115200 逐字节监听 ==="
Write-Host "请按 RESET 按钮..."

$port = New-Object System.IO.Ports.SerialPort "COM4", 115200, "None", 8, "One"
$port.ReadTimeout = 500
$port.Open()
Write-Host "已打开，等待数据..."

$startTime = Get-Date
$line = ""
$totalBytes = 0

try {
    while ($true) {
        try {
            $byte = $port.ReadByte()
            if ($byte -ge 0) {
                $totalBytes++
                $char = [char]$byte
                $line += $char
                # 实时显示每个字节
                Write-Host -NoNewline "$char"
                # 每收到10字节或换行时写入日志
                if ($line.Length -ge 10 -or $char -eq "`n" -or $char -eq "`r") {
                    $ts = Get-Date -Format "HH:mm:ss.fff"
                    "[$ts] $line" | Out-File -FilePath "C:\Users\gzf\Desktop\open-vela\boot_log.txt" -Append -Encoding utf8
                    $line = ""
                }
            }
        } catch {
            # ReadTimeout
        }
        
        $elapsed = ((Get-Date) - $startTime).TotalSeconds
        if ($elapsed -gt 10 -and $totalBytes -eq 0) {
            Write-Host "`n已等待10秒，0字节"
            break
        }
    }
} finally {
    if ($line.Length -gt 0) {
        $ts = Get-Date -Format "HH:mm:ss.fff"
        "[$ts] $line" | Out-File -FilePath "C:\Users\gzf\Desktop\open-vela\boot_log.txt" -Append -Encoding utf8
    }
    Write-Host "`n=== 共 $totalBytes 字节 ==="
    $port.Close()
}
