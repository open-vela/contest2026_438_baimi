# 测试 921600 波特率
Write-Host "=== 测试 COM4/921600 ==="
try {
    $port = New-Object System.IO.Ports.SerialPort "COM4", 921600, "None", 8, "One"
    $port.ReadTimeout = 5000
    $port.Open()
    Write-Host "COM4/921600 已打开，等待 5 秒..."

    $buf = New-Object byte[] 512
    $n = $port.Read($buf, 0, 512)
    Write-Host "读取到 $n 字节"
    if ($n -gt 0) {
        $text = [System.Text.Encoding]::ASCII.GetString($buf, 0, $n)
        Write-Host "内容: $text"
    }
    $port.Close()
} catch {
    Write-Host "错误: $_"
}
Write-Host "完成"
