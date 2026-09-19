# -*- coding: utf-8 -*-
"""串口监听脚本 - 捕获 D12x 板子启动日志"""
import serial
import time
import datetime

LOG_FILE = r"C:\Users\gzf\Desktop\open-vela\boot_log.txt"

def main():
    port = serial.Serial("COM4", 115200, timeout=1)
    port.reset_input_buffer()
    log = open(LOG_FILE, "w", encoding="utf-8")
    log.write("=== 串口监听 115200 ===\n")
    log.flush()
    print("=== 串口监听 115200，等待数据... ===")

    start = time.time()
    while time.time() - start < 90:
        data = port.read(256)
        if data:
            text = data.decode("utf-8", errors="replace")
            ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            line = f"[{ts}] {text}"
            print(line, end="")
            log.write(line)
            log.flush()

    log.close()
    port.close()
    print("\n=== 监听结束 ===")

if __name__ == "__main__":
    main()
