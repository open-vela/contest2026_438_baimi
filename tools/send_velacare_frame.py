"""Send one CRC-protected VelaCare gateway frame over a serial port.

Example:
    python send_velacare_frame.py COM7 --smoke alarm
"""

from __future__ import annotations

import argparse
import time


STATE_VALUES = {"offline": 0, "normal": 1, "alarm": 2}


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send a VelaCare VC1 sensor frame")
    parser.add_argument("port", nargs="?", help="Windows serial port, for example COM7")
    parser.add_argument("--seq", type=int, default=1)
    for name in ("smoke", "water", "door", "fall"):
        parser.add_argument(f"--{name}", choices=STATE_VALUES, default="normal")
    parser.add_argument("--wifi", choices=("offline", "online"), default="online")
    parser.add_argument("--repeat", type=int, default=5, help="Number of one-second heartbeats")
    parser.add_argument("--dry-run", action="store_true", help="Print frames without opening a serial port")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    states = [STATE_VALUES[getattr(args, name)] for name in ("smoke", "water", "door", "fall")]

    if not args.port and not args.dry_run:
        raise SystemExit("请提供串口号（例如 COM7），或使用 --dry-run 仅生成测试帧。")

    uart = None
    if not args.dry_run:
        try:
            import serial
        except ModuleNotFoundError as exc:
            raise SystemExit("缺少 pyserial；确认接线安全后可运行：python -m pip install pyserial") from exc
        uart = serial.Serial(args.port, 115200, timeout=0.25)

    try:
        for offset in range(args.repeat):
            seq = (args.seq + offset) & 0xFFFFFFFF
            payload = f"VC1,{seq},{states[0]},{states[1]},{states[2]},{states[3]},{int(args.wifi == 'online')}"
            frame = f"{payload}*{crc16_ccitt_false(payload.encode('ascii')):04X}\n"
            if uart is None:
                print(frame.rstrip())
                continue

            uart.write(frame.encode("ascii"))
            uart.flush()
            print("TX", frame.rstrip())

            reply = uart.readline().decode("ascii", errors="replace").strip()
            if reply:
                print("RX", reply)
            time.sleep(1)
    finally:
        if uart is not None:
            uart.close()


if __name__ == "__main__":
    main()
