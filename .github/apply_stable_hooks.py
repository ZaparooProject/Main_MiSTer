#!/usr/bin/env python3
"""Apply Zaparoo hooks to an upstream stable worktree.

The stable release build starts from an older upstream commit, so patch context
from upstream master can drift. This applies the small fork hooks by function
scope and fails if the expected target functions are not present.
"""

from __future__ import annotations

import sys
from pathlib import Path


ALT_INCLUDE = '#include "support/zaparoo/alt_launcher.h"\n'


def find_function_span(text: str, signature: str) -> tuple[int, int]:
    search_from = 0
    while True:
        start = text.find(signature, search_from)
        if start < 0:
            raise SystemExit(f"function signature not found: {signature}")

        brace = text.find("{", start)
        semicolon = text.find(";", start)
        if brace < 0:
            raise SystemExit(f"function body not found: {signature}")
        if semicolon >= 0 and semicolon < brace:
            search_from = semicolon + 1
            continue

        depth = 0
        for pos in range(brace, len(text)):
            ch = text[pos]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return start, pos + 1

        raise SystemExit(f"function body does not close: {signature}")


def replace_once(text: str, old: str, new: str, scope: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"target block not found in {scope}")
    return text.replace(old, new, 1)


def replace_once_in_function(text: str, signature: str, old: str, new: str) -> str:
    func_start, func_end = find_function_span(text, signature)
    before = text[:func_start]
    body = text[func_start:func_end]
    after = text[func_end:]

    updated = replace_once(body, old, new, signature)
    return before + updated + after


def add_include(text: str, after_include: str) -> str:
    if ALT_INCLUDE in text:
        return text
    return replace_once(text, after_include, after_include + ALT_INCLUDE, "includes")


def apply_input_hooks(path: Path) -> None:
    text = path.read_text()
    text = add_include(text, '#include "scaler.h"\n')

    text = replace_once_in_function(
        text,
        "static void joy_digital(",
        "\t\telse if (video_fb_state())\n"
        "\t\t{\n"
        "\t\t\tswitch (mask)\n",
        "\t\telse if (video_fb_state() || alt_launcher_active())\n"
        "\t\t{\n"
        "\t\t\tuint16_t alt_key = alt_launcher_fb_terminal_key(mask, bnum == BTN_OSD);\n"
        "\t\t\tif (alt_key)\n"
        "\t\t\t{\n"
        "\t\t\t\tuinp_send_key(alt_key, press);\n"
        "\t\t\t\treturn;\n"
        "\t\t\t}\n"
        "\n"
        "\t\t\tswitch (mask)\n",
    )

    text = replace_once_in_function(
        text,
        "static void input_cb(",
        "\t\t\t\tif (user_io_osd_is_visible() || video_fb_state())\n",
        "\t\t\t\tif (user_io_osd_is_visible() || video_fb_state() || alt_launcher_active())\n",
    )

    joy_start, joy_end = find_function_span(text, "static void joy_digital(")
    joy_body = text[joy_start:joy_end]
    if "alt_launcher_fb_terminal_key" not in joy_body:
        raise SystemExit("alt launcher key hook was not placed in joy_digital")
    if "setup_deadzone" in joy_body:
        raise SystemExit("unexpected setup_deadzone text inside joy_digital span")

    input_start, input_end = find_function_span(text, "static void input_cb(")
    input_body = text[input_start:input_end]
    if "user_io_osd_is_visible() || video_fb_state() || alt_launcher_active()" not in input_body:
        raise SystemExit("alt launcher active hook was not placed in input_cb")

    path.write_text(text)


def apply_scheduler_hooks(path: Path) -> None:
    text = path.read_text()
    text = add_include(text, '#include "profiling.h"\n')

    text = replace_once_in_function(
        text,
        "static void scheduler_co_poll(",
        "\t\t\tinput_poll(0);\n",
        "\t\t\tinput_poll(0);\n"
        "\t\t\talt_launcher_poll();\n",
    )

    start, end = find_function_span(text, "static void scheduler_co_poll(")
    body = text[start:end]
    if "alt_launcher_poll();" not in body:
        raise SystemExit("alt launcher poll hook was not placed in scheduler_co_poll")

    path.write_text(text)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: apply_stable_hooks.py <stable-worktree>", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    apply_input_hooks(root / "input.cpp")
    apply_scheduler_hooks(root / "scheduler.cpp")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
