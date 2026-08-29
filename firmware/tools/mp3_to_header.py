"""Convert an MP3 notification sound into a C array for the ES8311 firmware.

Decodes the input MP3 to 16 kHz mono signed 16-bit PCM, trims leading/trailing
silence, peak-normalizes, and emits a header (extern declarations) plus a
source file (the array definition) ready to drop into firmware/include and
firmware/src.

Requires: pip install miniaudio

The source MP3 is intentionally not committed to the repository; keep it
outside firmware/ and re-run this script if the notification sound changes.

Usage:
    python mp3_to_header.py input.mp3 \\
        --header firmware/include/notification_pcm.h \\
        --source firmware/src/notification_pcm.cpp
"""

import argparse

import miniaudio


def convert(input_path, header_path, source_path, symbol, peak, silence_threshold):
    dec = miniaudio.decode_file(input_path, output_format=miniaudio.SampleFormat.SIGNED16,
                                 nchannels=1, sample_rate=16000)
    samples = list(dec.samples)

    # Trim leading/trailing silence (below the threshold, full-scale units).
    start = next((i for i, s in enumerate(samples) if abs(s) > silence_threshold), 0)
    end = next((i for i in range(len(samples) - 1, -1, -1) if abs(samples[i]) > silence_threshold),
               len(samples) - 1) + 1
    samples = samples[start:end]

    # Peak-normalize so playback level is predictable.
    current_peak = max(abs(s) for s in samples) or 1
    gain = peak * 32767 / current_peak
    samples = [max(-32768, min(32767, round(s * gain))) for s in samples]

    len_symbol = f"{symbol}_LEN"

    with open(header_path, "w", newline="\n") as f:
        f.write("#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\n")
        f.write(f"// Generated from {input_path} by firmware/tools/mp3_to_header.py:\n")
        f.write("// decoded to 16 kHz mono s16, silence-trimmed, peak-normalized. Regenerate\n")
        f.write("// with the same script and arguments if the sound changes.\n")
        f.write(f"extern const int16_t {symbol}[];\n")
        f.write(f"extern const size_t {len_symbol};\n")

    with open(source_path, "w", newline="\n") as f:
        f.write(f'#include "notification_pcm.h"\n\n')
        f.write(f"const int16_t {symbol}[] = {{\n")
        for i in range(0, len(samples), 16):
            f.write("    " + ",".join(str(s) for s in samples[i:i + 16]) + ",\n")
        f.write("};\n")
        f.write(f"extern const size_t {len_symbol} = sizeof({symbol}) / sizeof({symbol}[0]);\n")

    print(f"samples={len(samples)} duration={len(samples) / 16000:.2f}s "
          f"header_bytes~{len(samples) * 2}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", help="path to the source MP3")
    parser.add_argument("--header", required=True, help="output path for the .h declarations")
    parser.add_argument("--source", required=True, help="output path for the .cpp definition")
    parser.add_argument("--symbol", default="NOTIFICATION_PCM",
                         help="C array symbol name (default: NOTIFICATION_PCM)")
    parser.add_argument("--peak", type=float, default=0.9,
                         help="peak-normalization target as a fraction of full scale (default: 0.9)")
    parser.add_argument("--silence-threshold", type=int, default=330,
                         help="silence trim threshold in full-scale sample units (default: 330)")
    args = parser.parse_args()

    convert(args.input, args.header, args.source, args.symbol, args.peak, args.silence_threshold)


if __name__ == "__main__":
    main()
