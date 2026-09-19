#!/usr/bin/env python3
"""Synthesize the VelaCare prompts, convert them to PCM WAV, and build the header."""

from __future__ import annotations

import argparse
import asyncio
import pathlib
import subprocess
import sys


PROMPTS = (
    ("fall", "\u68c0\u6d4b\u5230\u7591\u4f3c\u8dcc\u5012\uff0c\u8bf7\u95ee\u60a8\u8fd8\u597d\u5417\uff1f"),
    ("persistent", "\u6682\u672a\u6536\u5230\u56de\u5e94\uff0c\u6b63\u5728\u901a\u77e5\u5bb6\u4eba\u3002"),
    ("family", "\u5bb6\u4eba\u5df2\u7ecf\u6536\u5230\u6d88\u606f\uff0c\u8bf7\u4e0d\u8981\u7740\u6025\u3002"),
    ("elder", "\u597d\u7684\uff0c\u5df2\u7ecf\u5411\u5bb6\u4eba\u62a5\u544a\u60a8\u76ee\u524d\u5b89\u5168\u3002"),
)


async def synthesize_all(edge_tts, ffmpeg_exe: str, output_dir: pathlib.Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, text in PROMPTS:
        mp3_path = output_dir / f"{name}.mp3"
        wav_path = output_dir / f"{name}.wav"
        communicate = edge_tts.Communicate(
            text,
            voice="zh-CN-XiaoxiaoNeural",
            rate="-10%",
            volume="-12%",
        )
        await communicate.save(str(mp3_path))
        subprocess.run(
            [
                ffmpeg_exe,
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-i",
                str(mp3_path),
                "-acodec",
                "pcm_s16le",
                "-ar",
                "16000",
                "-ac",
                "1",
                str(wav_path),
            ],
            check=True,
        )
        mp3_path.unlink()
        print(f"Generated {wav_path.name}: {text}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-path", type=pathlib.Path)
    args = parser.parse_args()
    if args.runtime_path:
        sys.path.insert(0, str(args.runtime_path.resolve()))

    import edge_tts
    import imageio_ffmpeg

    script_root = pathlib.Path(__file__).resolve().parent
    sketch_root = script_root.parent
    output_dir = sketch_root / "voice_src"
    asyncio.run(synthesize_all(edge_tts, imageio_ffmpeg.get_ffmpeg_exe(), output_dir))
    subprocess.run(
        [
            sys.executable,
            str(script_root / "wav_to_voice_header.py"),
            "--input-dir",
            str(output_dir),
            "--output",
            str(sketch_root / "voice_prompts.h"),
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
