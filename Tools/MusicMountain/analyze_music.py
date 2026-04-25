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
import os
import re
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_OUTPUT = Path("Content/MusicMountain/Data/MusicAnalysisGenerated.json")
SECTION_NAMES = ("intro", "verse", "chorus", "bridge", "final")
DIRECTOR_MERGE_SECTION_FIELDS = ("mood", "energy", "terrain", "audio_style", "visual_motif", "gameplay_intent")
LRC_LINE_RE = re.compile(r"\[(\d{1,2}):(\d{2})(?:[.:](\d{1,3}))?\]\s*(.*)")


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


def parse_artist_title_from_filename(audio_path: Path) -> tuple[str, str]:
    stem = audio_path.stem.strip()
    separators = (" - ", " – ", " — ", "-", "–", "—")
    for separator in separators:
        if separator in stem:
            artist, title = stem.split(separator, 1)
            return artist.strip(), title.strip()
    return "", stem


def lrc_time_to_seconds(minutes: str, seconds: str, fraction: str | None) -> float:
    value = int(minutes) * 60 + int(seconds)
    if fraction:
        if len(fraction) == 1:
            value += int(fraction) / 10.0
        elif len(fraction) == 2:
            value += int(fraction) / 100.0
        else:
            value += int(fraction[:3]) / 1000.0
    return float(value)


def parse_synced_lyrics(synced_lyrics: str, duration: float) -> list[dict[str, Any]]:
    parsed_lines: list[tuple[float, str]] = []
    for raw_line in synced_lyrics.splitlines():
        match = LRC_LINE_RE.match(raw_line.strip())
        if not match:
            continue
        text = match.group(4).strip()
        if not text:
            continue
        parsed_lines.append((lrc_time_to_seconds(match.group(1), match.group(2), match.group(3)), text))

    lyrics: list[dict[str, Any]] = []
    for index, (start, text) in enumerate(parsed_lines):
        next_start = parsed_lines[index + 1][0] if index + 1 < len(parsed_lines) else min(start + 4.0, duration)
        end = clamp(next_start - 0.1, start + 1.2, start + 7.0)
        lyrics.append(
            {
                "start": round(start, 2),
                "end": round(min(end, duration), 2),
                "speaker": "Lyrics",
                "text": text,
                "mood": "",
            }
        )
    return lyrics


def plain_lyrics_to_timeline(plain_lyrics: str, duration: float) -> list[dict[str, Any]]:
    lines = [line.strip() for line in plain_lyrics.splitlines() if line.strip()]
    if not lines:
        return []

    usable_start = min(8.0, duration * 0.08)
    usable_duration = max(duration - usable_start, float(len(lines)) * 2.5)
    step = usable_duration / max(len(lines), 1)
    lyrics: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        start = usable_start + index * step
        end = min(start + max(min(step * 0.8, 5.5), 2.0), duration)
        lyrics.append(
            {
                "start": round(start, 2),
                "end": round(end, 2),
                "speaker": "Lyrics",
                "text": line,
                "mood": "",
            }
        )
    return lyrics


def search_lrclib(query_params: dict[str, str], duration: float) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    url = "https://lrclib.net/api/search?" + urllib.parse.urlencode(query_params)
    request = urllib.request.Request(url, headers={"User-Agent": "MusicMountainDemo/0.1"})
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            results = json.loads(response.read().decode("utf-8"))
    except Exception as exc:
        return [], {"status": "failed", "reason": str(exc), "query": query_params}

    if not isinstance(results, list) or not results:
        return [], {"status": "not_found", "query": query_params}

    best = results[0]
    synced = best.get("syncedLyrics") or ""
    plain = best.get("plainLyrics") or ""
    if synced:
        lyrics = parse_synced_lyrics(synced, duration)
        source_type = "synced"
    else:
        lyrics = plain_lyrics_to_timeline(plain, duration)
        source_type = "plain"

    return lyrics, {
        "status": "found" if lyrics else "empty",
        "source": "lrclib",
        "source_type": source_type,
        "query": query_params,
        "track": best.get("trackName", ""),
        "artist": best.get("artistName", ""),
        "album": best.get("albumName", ""),
    }


def build_direct_filename_lyrics_queries(audio_path: Path) -> list[dict[str, str]]:
    artist, title = parse_artist_title_from_filename(audio_path)
    queries: list[dict[str, str]] = []
    if title and artist:
        queries.append({"track_name": title, "artist_name": artist})
    if title:
        queries.append({"track_name": title})
    if audio_path.stem:
        queries.append({"q": audio_path.stem})
    return queries


def build_llm_lyrics_queries(audio_path: Path) -> tuple[list[dict[str, str]], dict[str, Any]]:
    prompt = (
        "You help search song lyrics. Return one JSON object only.\n"
        "Given an audio filename, infer likely artist/title and produce up to 5 LRCLIB search queries.\n"
        "Use fields q, track_name, artist_name only. Do not invent lyrics.\n"
        "Schema: {\"queries\":[{\"track_name\":\"...\",\"artist_name\":\"...\"},{\"q\":\"...\"}]}\n"
        f"Filename: {audio_path.name}\n"
    )
    try:
        result = call_openai_compatible_director(prompt)
    except Exception as exc:
        return [], {"llm_query_status": "failed", "llm_query_reason": str(exc)}

    queries: list[dict[str, str]] = []
    for raw_query in result.get("queries", []):
        if not isinstance(raw_query, dict):
            continue
        query: dict[str, str] = {}
        for key in ("q", "track_name", "artist_name"):
            value = raw_query.get(key)
            if isinstance(value, str) and value.strip():
                query[key] = value.strip()
        if query:
            queries.append(query)
    return queries, {"llm_query_status": "ok", "llm_queries": queries}


def lookup_lrclib_lyrics(audio_path: Path, duration: float, llm_query: bool = False) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    queries = build_direct_filename_lyrics_queries(audio_path)
    llm_metadata: dict[str, Any] = {"llm_query_status": "disabled"}
    if llm_query:
        llm_queries, llm_metadata = build_llm_lyrics_queries(audio_path)
        queries = llm_queries + queries

    if not queries:
        return [], {"status": "skipped", "reason": "empty filename query", **llm_metadata}

    seen: set[tuple[tuple[str, str], ...]] = set()
    failures: list[dict[str, Any]] = []
    for query_params in queries:
        signature = tuple(sorted(query_params.items()))
        if signature in seen:
            continue
        seen.add(signature)
        lyrics, metadata = search_lrclib(query_params, duration)
        metadata.update(llm_metadata)
        if lyrics:
            metadata["attempted_queries"] = queries
            return lyrics, metadata
        failures.append(metadata)

    return [], {
        "status": "not_found",
        "attempted_queries": queries,
        "failures": failures[:5],
        **llm_metadata,
    }


def build_music_json(audio_path: Path, features: AudioFeatures, style_hint: str = "", lookup_lyrics: bool = False, llm_lyrics_query: bool = False) -> dict[str, Any]:
    seed = stable_seed(audio_path)
    sections = build_sections(features, style_hint)
    theme_parts = sorted({section["mood"] for section in sections})
    normalized_hint = normalize_style_hint(style_hint)
    if normalized_hint and normalized_hint not in theme_parts:
        theme_parts.insert(0, normalized_hint)
    theme = ", ".join(theme_parts)
    lyrics: list[dict[str, Any]] = []
    lyrics_source: dict[str, Any] = {"status": "disabled"}
    if lookup_lyrics:
        lyrics, lyrics_source = lookup_lrclib_lyrics(audio_path, features.duration, llm_lyrics_query)

    payload = {
        "track": audio_path.stem,
        "display_name": audio_path.stem.replace("_", " ").replace("-", " ").title(),
        "bpm": round(features.bpm, 2),
        "theme": theme,
        "style_hint": style_hint or "",
        "lyrics_source": lyrics_source,
        "audio_event": "Play_MusicMountain_DemoSong",
        "mountain_plan": build_mountain_plan(features, seed, sections, style_hint),
        "sections": sections,
    }
    if lyrics:
        payload["lyrics"] = lyrics
    return payload


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


def extract_json_object(text: str) -> dict[str, Any]:
    stripped = text.strip()
    if stripped.startswith("```"):
        lines = stripped.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].startswith("```"):
            lines = lines[:-1]
        stripped = "\n".join(lines).strip()

    try:
        parsed = json.loads(stripped)
        if isinstance(parsed, dict):
            return parsed
    except json.JSONDecodeError:
        pass

    start = stripped.find("{")
    end = stripped.rfind("}")
    if start == -1 or end == -1 or end <= start:
        raise RuntimeError("LLM response did not contain a JSON object")

    parsed = json.loads(stripped[start : end + 1])
    if not isinstance(parsed, dict):
        raise RuntimeError("LLM response JSON was not an object")
    return parsed


def call_openai_compatible_director(prompt: str) -> dict[str, Any]:
    api_key = os.environ.get("MUSIC_MOUNTAIN_LLM_API_KEY", "").strip()
    endpoint = os.environ.get("MUSIC_MOUNTAIN_LLM_ENDPOINT", "https://api.openai.com/v1/chat/completions").strip()
    model = os.environ.get("MUSIC_MOUNTAIN_LLM_MODEL", "gpt-4o-mini").strip()
    provider = os.environ.get("MUSIC_MOUNTAIN_LLM_PROVIDER", "openai-compatible").strip()
    timeout = int(os.environ.get("MUSIC_MOUNTAIN_LLM_TIMEOUT", "90"))
    temperature = float(os.environ.get("MUSIC_MOUNTAIN_LLM_TEMPERATURE", "0.2"))

    if not api_key:
        raise RuntimeError("MUSIC_MOUNTAIN_LLM_API_KEY is empty. Configure Music Mountain Director settings in UE.")
    if not endpoint:
        raise RuntimeError("MUSIC_MOUNTAIN_LLM_ENDPOINT is empty")
    if not model:
        raise RuntimeError("MUSIC_MOUNTAIN_LLM_MODEL is empty")

    request_payload = {
        "model": model,
        "temperature": temperature,
        "messages": [
            {
                "role": "system",
                "content": "You are the Music Mountain LLM Director. Return one valid JSON object only.",
            },
            {
                "role": "user",
                "content": prompt,
            },
        ],
    }

    data = json.dumps(request_payload).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=data,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "User-Agent": f"MusicMountainDirector/{provider}",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            response_payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        error_body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"LLM HTTP {exc.code}: {error_body}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"LLM request failed: {exc}") from exc

    choices = response_payload.get("choices", [])
    if not choices:
        raise RuntimeError(f"LLM response contained no choices: {response_payload}")

    message = choices[0].get("message", {})
    content = message.get("content", "")
    if not content:
        raise RuntimeError(f"LLM response did not contain message content: {response_payload}")

    return extract_json_object(content)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Music Mountain JSON from an audio file.")
    parser.add_argument("audio", type=Path, help="Input audio path. WAV works without extra tools; MP3/MP4 usually need librosa + ffmpeg.")
    parser.add_argument("--output", "-o", type=Path, default=DEFAULT_OUTPUT, help=f"Output JSON path. Default: {DEFAULT_OUTPUT}")
    parser.add_argument("--style-hint", default="", help="Optional semantic style hint, e.g. romantic, sweet, melancholy, 情歌, 伤感.")
    parser.add_argument("--lookup-lyrics", action="store_true", help="Search LRCLIB by filename and import synced/plain lyrics into the output JSON.")
    parser.add_argument("--llm-lyrics-query", action="store_true", help="Use the configured LLM to infer LRCLIB lyric search queries from the audio filename.")
    parser.add_argument("--lyrics-file", type=Path, help="Optional lyrics or user description file to include in the LLM Director prompt.")
    parser.add_argument("--director-prompt-output", type=Path, help="Write a prompt that can be pasted into an LLM Director.")
    parser.add_argument("--director-json", type=Path, help="Merge an LLM Director JSON plan into the generated analysis output.")
    parser.add_argument("--call-llm-director", action="store_true", help="Call the configured OpenAI-compatible LLM Director and merge its JSON result.")
    parser.add_argument("--director-json-output", type=Path, help="Optional path to save the raw LLM Director JSON response.")
    parser.add_argument("--pretty", action="store_true", help="Print the generated JSON to stdout.")
    args = parser.parse_args()

    if not args.audio.exists():
        print(f"Audio file not found: {args.audio}", file=sys.stderr)
        return 2

    try:
        features = analyze_audio(args.audio)
        payload = build_music_json(args.audio, features, args.style_hint, args.lookup_lyrics, args.llm_lyrics_query)
        if args.director_prompt_output:
            lyrics = read_optional_text(args.lyrics_file)
            args.director_prompt_output.parent.mkdir(parents=True, exist_ok=True)
            director_prompt = build_director_prompt(payload, lyrics)
            args.director_prompt_output.write_text(director_prompt, encoding="utf-8")
        if args.call_llm_director:
            lyrics = read_optional_text(args.lyrics_file)
            director_prompt = build_director_prompt(payload, lyrics)
            director_plan = call_openai_compatible_director(director_prompt)
            if args.director_json_output:
                args.director_json_output.parent.mkdir(parents=True, exist_ok=True)
                args.director_json_output.write_text(json.dumps(director_plan, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            payload = merge_director_plan(payload, director_plan)
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
    lyrics_source = payload.get("lyrics_source", {})
    if lyrics_source:
        print("[lyrics]")
        print(f"status: {lyrics_source.get('status', 'unknown')}")
        print(f"query: {lyrics_source.get('query', {})}")
        if lyrics_source.get("source"):
            print(f"source: {lyrics_source.get('source')} ({lyrics_source.get('source_type', 'unknown')})")
        if lyrics_source.get("track") or lyrics_source.get("artist"):
            print(f"matched: {lyrics_source.get('artist', '')} - {lyrics_source.get('track', '')}")
        if lyrics_source.get("reason"):
            print(f"reason: {lyrics_source.get('reason')}")
        print(f"imported_lines: {len(payload.get('lyrics', []))}")
    if args.director_prompt_output:
        print(f"Wrote LLM Director prompt: {args.director_prompt_output}")
    if args.director_json:
        print(f"Merged LLM Director plan: {args.director_json}")
    if args.call_llm_director:
        print("Called configured LLM Director and merged response.")
    if args.director_json_output:
        print(f"Wrote raw LLM Director JSON: {args.director_json_output}")
    print(f"BPM: {payload['bpm']} | Seed: {payload['mountain_plan']['generation_seed']} | Theme: {payload['theme']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
