#!/usr/bin/env python3
"""Analyze an audio file and emit Music Mountain generation JSON.

The script prefers librosa for real MIR features. If librosa is unavailable,
it falls back to built-in WAV parsing for quick PCM WAV tests.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_OUTPUT = Path("Content/MusicMountain/Data/MusicAnalysisGenerated.json")
SECTION_NAMES = ("intro", "verse", "chorus", "bridge", "final")
DIRECTOR_MERGE_SECTION_FIELDS = ("mood", "energy", "terrain", "audio_style", "visual_motif", "gameplay_intent")


@dataclass
class AudioFeatures:
    duration: float
    bpm: float
    energy_curve: list[float]
    brightness_curve: list[float]


def clamp(value: float, lower: float, upper: float) -> float:
    return max(lower, min(upper, value))


def normalize_curve(values: list[float]) -> list[float]:
    if not values:
        return [0.5]

    min_value = min(values)
    max_value = max(values)
    if math.isclose(min_value, max_value):
        return [0.5 for _ in values]

    return [(value - min_value) / (max_value - min_value) for value in values]


def stable_seed(audio_path: Path) -> int:
    digest = hashlib.sha1()
    digest.update(str(audio_path.resolve()).encode("utf-8", errors="ignore"))
    with audio_path.open("rb") as handle:
        digest.update(handle.read(1024 * 1024))
    return int(digest.hexdigest()[:8], 16) % 1_000_000


def analyze_with_librosa(audio_path: Path) -> AudioFeatures:
    try:
        import librosa  # type: ignore
        import numpy as np  # type: ignore
    except ImportError as exc:
        raise RuntimeError("librosa is not installed") from exc

    samples, sample_rate = librosa.load(str(audio_path), sr=22050, mono=True)
    duration = float(librosa.get_duration(y=samples, sr=sample_rate))

    onset_env = librosa.onset.onset_strength(y=samples, sr=sample_rate)
    tempo_result = librosa.beat.tempo(onset_envelope=onset_env, sr=sample_rate)
    bpm = float(tempo_result[0]) if len(tempo_result) else 120.0

    rms = librosa.feature.rms(y=samples)[0]
    centroid = librosa.feature.spectral_centroid(y=samples, sr=sample_rate)[0]

    energy_curve = normalize_curve([float(value) for value in rms])
    brightness_curve = normalize_curve([float(value) for value in centroid])

    # Smooth curves into a manageable number of points for section sampling.
    target_points = 120
    if len(energy_curve) > target_points:
        indices = np.linspace(0, len(energy_curve) - 1, target_points).astype(int)
        energy_curve = [energy_curve[index] for index in indices]
    if len(brightness_curve) > target_points:
        indices = np.linspace(0, len(brightness_curve) - 1, target_points).astype(int)
        brightness_curve = [brightness_curve[index] for index in indices]

    return AudioFeatures(
        duration=max(duration, 1.0),
        bpm=clamp(bpm, 60.0, 210.0),
        energy_curve=energy_curve,
        brightness_curve=brightness_curve,
    )


def analyze_wav_fallback(audio_path: Path) -> AudioFeatures:
    if audio_path.suffix.lower() != ".wav":
        raise RuntimeError(
            "librosa is unavailable and fallback mode only supports PCM WAV. "
            "Install librosa + ffmpeg for mp3/mp4, or convert the file to wav first."
        )

    with wave.open(str(audio_path), "rb") as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        sample_rate = wav_file.getframerate()
        frame_count = wav_file.getnframes()
        raw = wav_file.readframes(frame_count)

    if sample_width != 2:
        raise RuntimeError("fallback WAV parser only supports 16-bit PCM WAV")

    samples = struct.unpack("<" + "h" * (len(raw) // 2), raw)
    if channels > 1:
        samples = samples[::channels]

    duration = frame_count / float(sample_rate)
    window = max(1024, sample_rate // 20)
    rms_values: list[float] = []
    zcr_values: list[float] = []

    for start in range(0, len(samples), window):
        chunk = samples[start : start + window]
        if not chunk:
            continue

        rms = math.sqrt(sum(float(sample) * float(sample) for sample in chunk) / len(chunk))
        zero_crossings = sum(
            1
            for index in range(1, len(chunk))
            if (chunk[index - 1] < 0 <= chunk[index]) or (chunk[index - 1] >= 0 > chunk[index])
        )
        rms_values.append(rms)
        zcr_values.append(zero_crossings / max(len(chunk), 1))

    energy_curve = normalize_curve(rms_values)
    brightness_curve = normalize_curve(zcr_values)

    # Tempo estimation without MIR is unreliable. Use energy pulse density only as a rough hint.
    threshold = statistics.mean(energy_curve) + statistics.pstdev(energy_curve) * 0.5
    peaks = sum(1 for value in energy_curve if value > threshold)
    bpm_hint = clamp((peaks / max(duration, 1.0)) * 60.0, 80.0, 160.0)

    return AudioFeatures(
        duration=max(duration, 1.0),
        bpm=bpm_hint,
        energy_curve=energy_curve,
        brightness_curve=brightness_curve,
    )


def convert_to_temp_wav(audio_path: Path) -> Path:
    ffmpeg_path = shutil.which("ffmpeg")
    if not ffmpeg_path:
        raise RuntimeError("ffmpeg was not found on PATH")

    temp_dir = Path(tempfile.mkdtemp(prefix="music_mountain_"))
    wav_path = temp_dir / f"{audio_path.stem}_music_mountain.wav"
    command = [
        ffmpeg_path,
        "-y",
        "-i",
        str(audio_path),
        "-vn",
        "-ac",
        "1",
        "-ar",
        "22050",
        "-sample_fmt",
        "s16",
        str(wav_path),
    ]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return wav_path


def decode_with_media_foundation(audio_path: Path) -> Path:
    decoder_path = Path(__file__).resolve().parent / "bin" / "mf_decode.exe"
    if not decoder_path.exists():
        raise RuntimeError(
            f"Windows Media Foundation decoder was not built: {decoder_path}. "
            "Run Tools/MusicMountain/WindowsMediaDecode/build.bat from a Visual Studio developer command prompt."
        )

    temp_dir = Path(tempfile.mkdtemp(prefix="music_mountain_mf_"))
    wav_path = temp_dir / f"{audio_path.stem}_mf.wav"
    subprocess.run([str(decoder_path), str(audio_path), str(wav_path)], check=True)
    return wav_path


def analyze_audio(audio_path: Path) -> AudioFeatures:
    errors: list[str] = []

    try:
        return analyze_with_librosa(audio_path)
    except Exception as librosa_error:
        errors.append(str(librosa_error))

    try:
        return analyze_wav_fallback(audio_path)
    except Exception as fallback_error:
        errors.append(str(fallback_error))

    try:
        wav_path = convert_to_temp_wav(audio_path)
        return analyze_wav_fallback(wav_path)
    except Exception as ffmpeg_error:
        errors.append(str(ffmpeg_error))

    try:
        wav_path = decode_with_media_foundation(audio_path)
        return analyze_wav_fallback(wav_path)
    except Exception as media_foundation_error:
        errors.append(str(media_foundation_error))

    raise RuntimeError("; ".join(errors))



def sample_curve(curve: list[float], start_alpha: float, end_alpha: float) -> float:
    if not curve:
        return 0.5

    start_index = int(clamp(start_alpha, 0.0, 1.0) * (len(curve) - 1))
    end_index = int(clamp(end_alpha, 0.0, 1.0) * (len(curve) - 1))
    if end_index < start_index:
        start_index, end_index = end_index, start_index

    values = curve[start_index : end_index + 1]
    return float(statistics.mean(values)) if values else curve[start_index]


def normalize_style_hint(style_hint: str | None) -> str:
    if not style_hint:
        return ""

    normalized = style_hint.strip().lower()
    if any(token in normalized for token in ("romantic", "love", "情歌", "爱情", "恋爱", "温柔", "浪漫")):
        return "romantic"
    if any(token in normalized for token in ("sweet", "甜")):
        return "sweet"
    if any(token in normalized for token in ("melancholy", "sad", "失恋", "伤感", "悲伤", "emo")):
        return "melancholy"
    if any(token in normalized for token in ("dreamy", "梦幻", "云", "治愈", "chill")):
        return "dreamy"
    if any(token in normalized for token in ("modern", "mordern", "摩登", "现代")):
        return "modern"
    if any(token in normalized for token in ("pop", "流行")):
        return "pop"
    if any(token in normalized for token in ("electronic", "edm", "synth", "电子")):
        return "electronic"
    if any(token in normalized for token in ("rock", "metal", "摇滚")):
        return "rock"
    if any(token in normalized for token in ("acoustic", "folk", "民谣", "木吉他")):
        return "acoustic"
    if any(token in normalized for token in ("classical", "orchestral", "piano", "古典", "钢琴")):
        return "classical"
    if any(token in normalized for token in ("epic", "史诗", "燃", "热血")):
        return "epic"
    if any(token in normalized for token in ("dark", "黑暗", "压抑", "恐怖")):
        return "dark"
    return normalized


def classify_mood(energy: float, brightness: float, section_index: int, style_hint: str = "") -> str:
    style = normalize_style_hint(style_hint)
    if style == "romantic":
        if section_index == 0:
            return "sweet"
        if energy > 0.72:
            return "romantic"
        return "dreamy"
    if style == "sweet":
        return "sweet" if energy < 0.76 else "romantic"
    if style == "melancholy":
        return "melancholy" if energy < 0.75 else "romantic"
    if style == "dreamy":
        return "dreamy"
    if style == "modern":
        return "modern" if energy < 0.78 else "pop"
    if style == "pop":
        return "pop"
    if style == "electronic":
        return "electronic" if energy > 0.55 else "modern"
    if style == "rock":
        return "rock" if energy > 0.5 else "dark"
    if style == "acoustic":
        return "acoustic" if energy < 0.75 else "romantic"
    if style == "classical":
        return "classical" if energy < 0.8 else "epic"
    if style == "epic" and energy > 0.58:
        return "epic"
    if style == "dark":
        return "dark"

    if section_index == 0 and energy < 0.55:
        return "calm"
    if energy > 0.78 and brightness > 0.55:
        return "epic"
    if energy > 0.7:
        return "tense"
    if brightness < 0.38:
        return "dark"
    return "uplifting" if section_index == len(SECTION_NAMES) - 1 else "calm"


def terrain_for(mood: str, energy: float) -> str:
    if mood == "romantic":
        return "sunset_flower_spiral"
    if mood == "sweet":
        return "soft_meadow_spiral"
    if mood == "melancholy":
        return "misty_lakeside_spiral"
    if mood == "dreamy":
        return "cloud_garden_spiral"
    if mood == "modern":
        return "glass_city_spiral"
    if mood == "pop":
        return "neon_city_ridge"
    if mood == "electronic":
        return "cyber_neon_spiral"
    if mood == "rock":
        return "broken_rock_ridge"
    if mood == "acoustic":
        return "woodland_meadow_spiral"
    if mood == "classical":
        return "marble_garden_spiral"
    if mood == "calm":
        return "forest_spiral_slope"
    if mood == "dark":
        return "shadow_cliff_path"
    if mood == "epic":
        return "wide_spiral_ridge" if energy < 0.9 else "vertical_spiral_ridge"
    if mood == "tense":
        return "collapsed_cave_spiral"
    return "summit_spire"


def audio_style_for(mood: str, energy: float) -> str:
    if mood == "romantic":
        return "warm reverb, soft stereo, glowing ambience"
    if mood == "sweet":
        return "gentle low-pass, light air, soft bloom"
    if mood == "melancholy":
        return "wide reverb, muted highs, distant rain"
    if mood == "dreamy":
        return "shimmer reverb, soft chorus, cloud ambience"
    if mood == "modern":
        return "clean stereo, polished ambience, smooth transient"
    if mood == "pop":
        return "bright mix, glossy rhythm, city ambience"
    if mood == "electronic":
        return "sidechain pulse, neon synth space, crisp highs"
    if mood == "rock":
        return "wide guitars, rough edges, impact hits"
    if mood == "acoustic":
        return "warm room, soft strings, natural air"
    if mood == "classical":
        return "hall reverb, orchestral space, elegant bloom"
    if mood == "calm":
        return "soft low-pass, airy ambience"
    if mood == "dark":
        return "dark EQ, narrow stereo, distant rock fall"
    if mood == "epic":
        return "open mix, stronger drums, high wind"
    if mood == "tense":
        return "cave reverb, low rumble, compressed highs"
    return "bright full mix, summit swell"


def build_sections(features: AudioFeatures, style_hint: str = "") -> list[dict[str, Any]]:
    sections: list[dict[str, Any]] = []
    section_count = len(SECTION_NAMES)

    for index, name in enumerate(SECTION_NAMES):
        start_alpha = index / section_count
        end_alpha = (index + 1) / section_count
        energy = sample_curve(features.energy_curve, start_alpha, end_alpha)
        brightness = sample_curve(features.brightness_curve, start_alpha, end_alpha)
        mood = classify_mood(energy, brightness, index, style_hint)

        sections.append(
            {
                "name": name,
                "start": round(features.duration * start_alpha, 2),
                "end": round(features.duration * end_alpha, 2),
                "mood": mood,
                "energy": round(clamp(0.25 + energy * 0.75, 0.0, 1.0), 3),
                "terrain": terrain_for(mood, energy),
                "audio_style": audio_style_for(mood, energy),
            }
        )

    return sections


def build_mountain_plan(features: AudioFeatures, seed: int, sections: list[dict[str, Any]], style_hint: str = "") -> dict[str, Any]:
    average_energy = statistics.mean(float(section["energy"]) for section in sections)
    energy_spread = max(float(section["energy"]) for section in sections) - min(float(section["energy"]) for section in sections)
    tempo_alpha = clamp((features.bpm - 80.0) / 100.0, 0.0, 1.0)
    style = normalize_style_hint(style_hint)

    road_width_bonus = 0
    outer_slope_bias = 0
    inner_wall_bias = 0
    pitch_bias = 0
    variation_bias = 0.0
    if style in ("romantic", "sweet", "dreamy", "melancholy", "acoustic", "classical"):
        road_width_bonus = 90
        outer_slope_bias = -120
        inner_wall_bias = -180
        pitch_bias = -2
        variation_bias = -0.035
    elif style in ("modern", "pop", "electronic"):
        road_width_bonus = 40
        outer_slope_bias = 40
        inner_wall_bias = -60
        pitch_bias = 0
        variation_bias = 0.02
    elif style == "rock":
        road_width_bonus = -20
        outer_slope_bias = 140
        inner_wall_bias = 120
        pitch_bias = 1
        variation_bias = 0.06

    return {
        "generation_seed": seed,
        "mountain_height": round(6500 + average_energy * 3200 + tempo_alpha * 1800),
        "base_path_radius": round(2600 + (1.0 - average_energy) * 1200),
        "top_path_radius": round(850 + (1.0 - tempo_alpha) * 900),
        "total_turns": round(1.6 + tempo_alpha * 1.4 + energy_spread * 0.7, 2),
        "segments_per_turn": round(32 + tempo_alpha * 18),
        "road_width": round(680 - average_energy * 190 + road_width_bonus),
        "outer_slope_width": round(500 + average_energy * 420 + outer_slope_bias),
        "inner_wall_height": round(650 + average_energy * 650 + inner_wall_bias),
        "elevation_gain_multiplier": round(1.15 + average_energy * 0.45, 2),
        "max_ramp_pitch_degrees": round(20 + tempo_alpha * 5 + pitch_bias),
        "visibility_range_meters": 1600,
        "variation": {
            "radius": round(clamp(0.12 + energy_spread * 0.28 + variation_bias, 0.06, 0.38), 3),
            "height": round(clamp(0.04 + energy_spread * 0.16 + variation_bias * 0.5, 0.02, 0.18), 3),
            "road_width": round(clamp(0.06 + average_energy * 0.16 + variation_bias, 0.03, 0.22), 3),
            "core": round(clamp(0.16 + energy_spread * 0.26 + variation_bias, 0.08, 0.42), 3),
        },
    }


def build_music_json(audio_path: Path, features: AudioFeatures, style_hint: str = "") -> dict[str, Any]:
    seed = stable_seed(audio_path)
    sections = build_sections(features, style_hint)
    theme_parts = sorted({section["mood"] for section in sections})
    normalized_hint = normalize_style_hint(style_hint)
    if normalized_hint and normalized_hint not in theme_parts:
        theme_parts.insert(0, normalized_hint)
    theme = ", ".join(theme_parts)

    return {
        "track": audio_path.stem,
        "display_name": audio_path.stem.replace("_", " ").replace("-", " ").title(),
        "bpm": round(features.bpm, 2),
        "theme": theme,
        "style_hint": style_hint or "",
        "audio_event": "Play_MusicMountain_DemoSong",
        "mountain_plan": build_mountain_plan(features, seed, sections, style_hint),
        "sections": sections,
    }


def read_optional_text(path: Path | None) -> str:
    if not path:
        return ""
    return path.read_text(encoding="utf-8").strip()


def build_audio_summary(payload: dict[str, Any]) -> dict[str, Any]:
    sections = payload.get("sections", [])
    return {
        "track": payload.get("track", ""),
        "display_name": payload.get("display_name", ""),
        "bpm": payload.get("bpm", 120),
        "style_hint": payload.get("style_hint", ""),
        "rule_based_theme": payload.get("theme", ""),
        "rule_based_mountain_plan": payload.get("mountain_plan", {}),
        "rule_based_sections": [
            {
                "name": section.get("name", ""),
                "start": section.get("start", 0),
                "end": section.get("end", 0),
                "energy": section.get("energy", 0.5),
                "mood": section.get("mood", ""),
                "terrain": section.get("terrain", ""),
                "audio_style": section.get("audio_style", ""),
            }
            for section in sections
        ],
    }


def build_director_prompt(payload: dict[str, Any], lyrics: str = "") -> str:
    schema = {
        "overall_theme": "short semantic theme, e.g. romantic city night climb",
        "journey_arc": "one sentence describing the emotional playable climb",
        "theme": "comma-separated moods for UE HUD",
        "mountain_plan": {
            "generation_seed": 123456,
            "mountain_height": 8600,
            "base_path_radius": 3200,
            "top_path_radius": 1400,
            "total_turns": 2.4,
            "segments_per_turn": 44,
            "road_width": 560,
            "outer_slope_width": 620,
            "inner_wall_height": 900,
            "elevation_gain_multiplier": 1.35,
            "max_ramp_pitch_degrees": 23,
            "visibility_range_meters": 1600,
            "variation": {
                "radius": 0.18,
                "height": 0.08,
                "road_width": 0.12,
                "core": 0.24,
            },
        },
        "sections": [
            {
                "name": "intro",
                "mood": "sweet",
                "energy": 0.55,
                "terrain": "soft_meadow_spiral",
                "audio_style": "gentle low-pass, light air, soft bloom",
                "visual_motif": "warm grass, floating petals, sunset haze",
                "gameplay_intent": "safe warmup with broad road",
            }
        ],
    }

    prompt_parts = [
        "You are the Music Mountain LLM Director.",
        "",
        "Goal: turn low-level audio features into a playable mountain journey for Unreal Engine.",
        "Do not write prose outside JSON. Return one valid JSON object only.",
        "",
        "Design intent:",
        "- The player climbs a spiral mountain while the imported song plays.",
        "- Audio features provide tempo and energy, but you supply semantic mood, terrain, visual motif, and gameplay intent.",
        "- Keep the output playable: avoid extreme narrow roads unless the section is intentionally dangerous.",
        "- Preserve the section count and section names from the input summary.",
        "- You may override mountain_plan values if the music semantics justify it.",
        "",
        "Allowed mood vocabulary:",
        "romantic, sweet, melancholy, dreamy, modern, pop, electronic, rock, acoustic, classical, calm, dark, epic, tense, uplifting",
        "",
        "Terrain names should be short snake_case phrases, e.g. sunset_flower_spiral, cloud_garden_spiral, shadow_cliff_path.",
        "",
        "Output JSON schema example:",
        json.dumps(schema, ensure_ascii=False, indent=2),
        "",
        "Input audio summary:",
        json.dumps(build_audio_summary(payload), ensure_ascii=False, indent=2),
    ]

    if lyrics:
        prompt_parts.extend(["", "Optional lyrics or user description:", lyrics])

    return "\n".join(prompt_parts) + "\n"


def merge_director_plan(payload: dict[str, Any], director_plan: dict[str, Any]) -> dict[str, Any]:
    merged = json.loads(json.dumps(payload, ensure_ascii=False))

    if isinstance(director_plan.get("theme"), str):
        merged["theme"] = director_plan["theme"]
    elif isinstance(director_plan.get("overall_theme"), str):
        merged["theme"] = director_plan["overall_theme"]

    merged["director"] = {
        "source": "llm",
        "overall_theme": director_plan.get("overall_theme", ""),
        "journey_arc": director_plan.get("journey_arc", ""),
    }

    if isinstance(director_plan.get("mountain_plan"), dict):
        mountain_plan = merged.setdefault("mountain_plan", {})
        mountain_plan.update(director_plan["mountain_plan"])

    director_sections = director_plan.get("sections", [])
    if isinstance(director_sections, list):
        merged_sections = merged.setdefault("sections", [])
        for index, director_section in enumerate(director_sections):
            if index >= len(merged_sections) or not isinstance(director_section, dict):
                continue
            for key in DIRECTOR_MERGE_SECTION_FIELDS:
                if key in director_section:
                    merged_sections[index][key] = director_section[key]

    return merged


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Music Mountain JSON from an audio file.")
    parser.add_argument("audio", type=Path, help="Input audio path. WAV works without extra tools; MP3/MP4 usually need librosa + ffmpeg.")
    parser.add_argument("--output", "-o", type=Path, default=DEFAULT_OUTPUT, help=f"Output JSON path. Default: {DEFAULT_OUTPUT}")
    parser.add_argument("--style-hint", default="", help="Optional semantic style hint, e.g. romantic, sweet, melancholy, 情歌, 伤感.")
    parser.add_argument("--lyrics-file", type=Path, help="Optional lyrics or user description file to include in the LLM Director prompt.")
    parser.add_argument("--director-prompt-output", type=Path, help="Write a prompt that can be pasted into an LLM Director.")
    parser.add_argument("--director-json", type=Path, help="Merge an LLM Director JSON plan into the generated analysis output.")
    parser.add_argument("--pretty", action="store_true", help="Print the generated JSON to stdout.")
    args = parser.parse_args()

    if not args.audio.exists():
        print(f"Audio file not found: {args.audio}", file=sys.stderr)
        return 2

    try:
        features = analyze_audio(args.audio)
        payload = build_music_json(args.audio, features, args.style_hint)
        if args.director_prompt_output:
            lyrics = read_optional_text(args.lyrics_file)
            args.director_prompt_output.parent.mkdir(parents=True, exist_ok=True)
            args.director_prompt_output.write_text(build_director_prompt(payload, lyrics), encoding="utf-8")
        if args.director_json:
            director_plan = json.loads(args.director_json.read_text(encoding="utf-8"))
            payload = merge_director_plan(payload, director_plan)
    except Exception as exc:
        print(f"Failed to analyze audio: {exc}", file=sys.stderr)
        print("For mp3/mp4 support, install ffmpeg and Python packages from Tools/MusicMountain/requirements.txt.", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if args.pretty:
        print(json.dumps(payload, ensure_ascii=False, indent=2))

    print(f"Wrote Music Mountain analysis: {args.output}")
    if args.director_prompt_output:
        print(f"Wrote LLM Director prompt: {args.director_prompt_output}")
    if args.director_json:
        print(f"Merged LLM Director plan: {args.director_json}")
    print(f"BPM: {payload['bpm']} | Seed: {payload['mountain_plan']['generation_seed']} | Theme: {payload['theme']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
