#!/usr/bin/env python3
"""
deps:
    pillow
"""

import argparse

from PIL import Image

RESET = "\033[0m"


def fg(r, g, b):
    return f"\033[38;2;{r};{g};{b}m"


def bg(r, g, b):
    return f"\033[48;2;{r};{g};{b}m"


def escape_c_string(s: str) -> str:
    s = s.replace("\\", "\\\\")
    s = s.replace("\"", "\\\"")
    s = s.replace("\n", "\\n\"\n\"")
    return s


def convert_image(path, width=80, aspect=1.0, double=True, c_string=False):
    img = Image.open(path).convert("RGB")

    # If double resolution: we use rows in pairs
    if double:
        height = int(img.height * (width / img.width))
        height = height if height % 2 == 0 else height - 1
        img = img.resize((width, height))
        pixels = img.load()

        out_lines = []
        for y in range(0, height, 2):
            line = ""
            last_fg = None
            last_bg = None

            for x in range(width):
                r1, g1, b1 = pixels[x, y]
                r2, g2, b2 = pixels[x, y + 1]

                fg_code = fg(r1, g1, b1)
                bg_code = bg(r2, g2, b2)

                # Only emit changed fg/bg codes
                if fg_code != last_fg:
                    line += fg_code
                    last_fg = fg_code
                if bg_code != last_bg:
                    line += bg_code
                    last_bg = bg_code

                line += "▀"  # upper half-block

            line += RESET
            out_lines.append(line)

        output = "\n".join(out_lines)

    else:
        # fallback: single-res mode (previous behavior)
        h = int(img.height * (width / img.width) * aspect)
        img = img.resize((width, h))
        pixels = img.load()

        out_lines = []
        for y in range(h):
            line = ""
            last_ansi = None
            for x in range(width):
                r, g, b = pixels[x, y]
                ansi = fg(r, g, b)
                if ansi != last_ansi:
                    line += ansi
                    last_ansi = ansi
                line += "█"
            line += RESET
            out_lines.append(line)

        output = "\n".join(out_lines)

    if c_string:
        return "\"" + escape_c_string(output) + "\""

    return output


def main():
    p = argparse.ArgumentParser(
        description="Convert an image to double-resolution ANSI unicode pixel art."
    )
    p.add_argument("image")
    p.add_argument("--width", type=int, default=80)
    p.add_argument("--no-double", action="store_true", help="Disable double-resolution mode")
    p.add_argument("--aspect", type=float, default=1.0)
    p.add_argument(
        "--c-string", action="store_true", help="Emit output formatted as a C string literal"
    )
    args = p.parse_args()

    out = convert_image(
        args.image,
        width=args.width,
        aspect=args.aspect,
        double=not args.no_double,
        c_string=args.c_string,
    )
    print(out)


if __name__ == "__main__":
    main()
