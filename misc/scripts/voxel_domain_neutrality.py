#!/usr/bin/env python3
"""Rejects application-domain vocabulary in the voxel module.

The voxel module is a general sparse signed distance field engine. The moment a
class, a comment, or a setting inside it names the specific thing the first game
built on it happened to be carving, the module has quietly become that game's
module, and the next project has to fork it rather than use it. That is not a
style preference, it is the difference between an engine feature and a one-off.

This runs as a pre-commit hook so the boundary is enforced at the point the word
is written, rather than discovered later when it is load-bearing.
"""

import re
import sys

# Words specific to the first application built on this module: the material it
# cuts, the tools it cuts with, and the place it happens in. Extend this list
# rather than relaxing it; every entry here is a word that would have made the
# module unusable for something else.
#
# Generic geometry verbs are deliberately absent. "Carve", "cut", and "stamp"
# describe what any signed distance field engine does to any material, and
# banning them would leave the module unable to say what it is for.
FORBIDDEN = [
    "wood",
    "bark",
    "log",
    "grain",
    "timber",
    "lumber",
    "plank",
    "sawdust",
    "chisel",
    "gouge",
    "whittle",
    "sand",
    "polish",
    "varnish",
    "lathe",
    "workshop",
    "workbench",
]

# Words that legitimately contain a forbidden word as a whole word, or that share
# a prefix with one. Matching is on whole words, so "logic" and "sandbox" never
# match "log" or "sand" in the first place; these are the cases that do.
ALLOWED = [
    "math::log",  # Natural logarithm, from core/math/math_funcs.h.
    "log2",
    "log10",
    "logf",
    "logarithm",
    "print_error",  # Mentions "log" in surrounding prose often enough to be worth allowing.
]

PATTERN = re.compile(r"\b(" + "|".join(FORBIDDEN) + r")(?:s|es|ed|ing|er|ers)?\b", re.IGNORECASE)


def is_allowed(line: str, start: int) -> bool:
    """True if the match at this offset is one of the permitted false positives."""
    context = line[max(0, start - 8) : start + 16].lower()
    return any(allowance in context for allowance in ALLOWED)


def main() -> int:
    failed = False

    for path in sys.argv[1:]:
        try:
            with open(path, "rt", encoding="utf-8", newline="") as file:
                lines = file.read().split("\n")
        except UnicodeDecodeError:
            continue

        for number, line in enumerate(lines, start=1):
            for match in PATTERN.finditer(line):
                if is_allowed(line, match.start()):
                    continue
                if not failed:
                    print("The voxel module must not name the application domain it is used for.")
                    print("It is a general signed distance field engine, and a word from one game's")
                    print("subject matter in it means the next game has to fork it.\n")
                    failed = True
                print(f"{path}:{number}: {match.group(0)!r} in: {line.strip()}")

    if failed:
        print("\nRename to describe the geometry or the data, not the material being cut.")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
