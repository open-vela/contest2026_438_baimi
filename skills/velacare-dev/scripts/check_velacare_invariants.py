#!/usr/bin/env python3
"""Fail common VelaCare workflow mistakes before flash or contest packaging."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

WINDOWS_D12X = Path("tmp/v070-freeze-fix-src/app/velacare")
ESP32_INO = Path("esp32_velacare_gateway/esp32_velacare_gateway.ino")
SKILL_ROOT = Path("_contest_stage/skills/velacare-dev")
DRAFT_ROOT = Path("_contest_stage/data/agent/skills")
CURRENT_ESP32 = "0.15.1"
SAFE_ANCHOR = ("0.149537", "0.059815", "0.986945")
FALL_ANCHOR = ("-0.957276", "-0.199432", "0.209404")


def rel(root: Path, path: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def add(errors, ok, cond, fail, passed) -> None:
    if cond:
        ok.append(passed)
    else:
        errors.append(fail)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def makefile_links_tts(makefile: str) -> bool:
    linked = [line.split("#", 1)[0] for line in makefile.splitlines()]
    blob = "\n".join(linked)
    return bool(re.search(r"velacare_tts\.c|velacare_voice_pcm\.c", blob))


def collect_docs(root: Path):
    docs = []
    for folder in (SKILL_ROOT, DRAFT_ROOT):
        base = root / folder
        if not base.exists():
            continue
        docs.extend(path for path in base.rglob("*.md") if path.is_file())
    docs.extend(root.glob("VelaCare*.md"))
    docs.extend(root.glob("*" + "\u70e7\u5f55" + "*.md"))
    return sorted(set(docs))


def extract_ap_password(ino: str):
    match = re.search(r'kConfigApPassword\[\]\s*=\s*"([^"]+)"', ino)
    return match.group(1) if match else None


def mentions_mpu6050_as_claim(body: str, name: str) -> bool:
    if not re.search(r"MPU6050|mpu6050", body):
        return False
    normalized = name.replace("\\", "/")
    if "velacare-dev" in normalized:
        return False
    lowered = body.lower()
    if "never write" in lowered or "do not write" in lowered or "must not" in lowered:
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check VelaCare firmware/docs invariants for team baimi."
    )
    parser.add_argument(
        "--root",
        default=".",
        help="Windows workspace root (default: current directory)",
    )
    args = parser.parse_args()
    root = Path(args.root).resolve()
    errors = []
    ok = []

    d12x = root / WINDOWS_D12X
    makefile = d12x / "Makefile"
    main_c = d12x / "velacare_main.c"
    sensor_c = d12x / "velacare_sensor.c"
    ino_path = root / ESP32_INO

    add(errors, ok, makefile.is_file() and main_c.is_file() and sensor_c.is_file(),
        "missing D12X work files under " + str(WINDOWS_D12X),
        "D12X work tree present: " + str(WINDOWS_D12X))
    add(errors, ok, ino_path.is_file(),
        "missing ESP32 source " + str(ESP32_INO),
        "ESP32 source present: " + str(ESP32_INO))

    if makefile.is_file():
        text = read(makefile)
        add(errors, ok, not makefile_links_tts(text),
            "D12X Makefile still links velacare_tts.c or velacare_voice_pcm.c",
            "D12X Makefile does not link TTS/PCM")
        add(errors, ok, "velacare_settings.c" in text,
            "D12X Makefile missing velacare_settings.c",
            "D12X Makefile includes settings")

    if main_c.is_file():
        text = read(main_c)
        add(errors, ok, "VelaCare v0.8.0 family-care" in text,
            "velacare_main.c is not labeled v0.8.0 family-care",
            "D12X banner is v0.8.0 family-care")
        add(errors, ok,
            "VCB1 mute/ack stops the local buzzer only" in text or "UI still shows real alarms" in text,
            "velacare_main.c lost the VCB1-does-not-rewrite-sensors comment",
            "D12X still documents VCB1 as buzzer-only")

    if sensor_c.is_file():
        text = read(sensor_c)
        add(errors, ok, 'sscanf(line, "VC1,%lu,%u,%u,%u,%u,%u%c"' in text,
            "VC1 field order parser missing or changed",
            "VC1 parser still smoke,water,door,fall,wifi")
        add(errors, ok, "VCB1,%u,%u,%u" in text,
            "VCB1 parser missing",
            "VCB1 parser present")
        add(errors, ok, "OK1," in text and "FACK1," in text,
            "OK1/FACK1 handling missing from D12X sensor code",
            "OK1 and FACK1 handling present")

    if ino_path.is_file():
        ino = read(ino_path)
        add(errors, ok, 'kFirmwareVersion[] = "' + CURRENT_ESP32 + '"' in ino,
            "ESP32 kFirmwareVersion is not " + CURRENT_ESP32,
            "ESP32 version is " + CURRENT_ESP32)
        add(errors, ok, all(v in ino for v in SAFE_ANCHOR) and all(v in ino for v in FALL_ANCHOR),
            "ESP32 dual-anchor constants missing or changed",
            "ESP32 dual-anchor constants present")
        add(errors, ok, "kMpu6500WhoAmI" in ino and "kMpu9250WhoAmI" in ino,
            "ESP32 IMU identity is not MPU-6500/9250",
            "ESP32 IMU is MPU-6500/9250")
        add(errors, ok, "MPU6050" not in ino and "mpu6050" not in ino.lower(),
            "ESP32 source mentions MPU6050",
            "ESP32 source does not say MPU6050")
        add(errors, ok,
            "kWaterSensorPin = 4" in ino and "kSmokeSensorPin = 5" in ino
            and "kDoorSensorPin = 6" in ino and "kImuSdaPin = 8" in ino
            and "kGatewayTxPin = 17" in ino,
            "ESP32 GPIO map does not match the bench wiring",
            "ESP32 GPIO map matches smoke5/leak4/door6/IMU8-9/UART17-18")
        add(errors, ok, "VelaCare-Setup-" in ino,
            "setup AP SSID prefix missing",
            "setup AP SSID prefix is VelaCare-Setup-")
        password = extract_ap_password(ino)
        if password:
            leaked = []
            for doc in collect_docs(root):
                if password in read(doc):
                    leaked.append(rel(root, doc))
            add(errors, ok, not leaked,
                "hotspot password leaked into " + ", ".join(leaked),
                "hotspot password not copied into docs/skills")

    for path in collect_docs(root):
        name = rel(root, path)
        if mentions_mpu6050_as_claim(read(path), name):
            errors.append(name + " mentions MPU6050")

    drafts = [
        "elder-care-reminder.md",
        "emergency-broadcast.md",
        "home-safety-guard.md",
        "routine-scheduler.md",
        "README.md",
    ]
    for name in drafts:
        path = root / DRAFT_ROOT / name
        add(errors, ok, path.is_file() and path.stat().st_size > 0,
            "missing design-time draft " + str(DRAFT_ROOT / name),
            "design-time draft present: " + name)
    readme = root / DRAFT_ROOT / "README.md"
    if readme.is_file():
        text = read(readme)
        add(errors, ok, "does NOT load" in text or "does not load" in text.lower(),
            "skills README does not say v0.8.0 firmware does not load ai_agent",
            "skills README states firmware does not load ai_agent")

    skill_md = root / SKILL_ROOT / "SKILL.md"
    add(errors, ok, skill_md.is_file(),
        "velacare-dev SKILL.md missing",
        "velacare-dev SKILL.md present")

    print("VelaCare invariant check")
    print("root: " + str(root))
    for line in ok:
        print("OK  " + line)
    if errors:
        for line in errors:
            print("ERR " + line)
        print("FAILED (" + str(len(errors)) + " error(s), " + str(len(ok)) + " passed)")
        return 1
    print("PASSED (" + str(len(ok)) + " checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
