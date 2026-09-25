"""Download ``VapourSynth4.h`` and ``VSHelper4.h`` for builds with no VapourSynth installation."""

from __future__ import annotations

from pathlib import Path
from urllib.request import urlopen
import argparse
import sys

__all__ = ('main',)

HEADERS = ('VapourSynth4.h', 'VSHelper4.h')
"""VapourSynth v4 API headers this plugin builds against.

:meta hide-value:
"""

URL_TEMPLATE = 'https://raw.githubusercontent.com/vapoursynth/vapoursynth/{ref}/include/{header}'
"""Template of the raw header URL, parameterised by Git ref and header name.

:meta hide-value:
"""
TIMEOUT = 30
"""Socket timeout in seconds, so a stalled network fails the build instead of hanging it.

:meta hide-value:
"""


def main() -> int:
    """
    Download the VapourSynth headers into the requested directory.

    Returns
    -------
    int
        Process exit status.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', default='R72', help='Git ref of the VapourSynth repository.')
    parser.add_argument(
        '--destination', required=True, type=Path, help='Directory to write the headers into.'
    )
    args = parser.parse_args()
    if all((args.destination / header).is_file() for header in HEADERS):
        return 0
    args.destination.mkdir(parents=True, exist_ok=True)
    for header in HEADERS:
        target = args.destination / header
        if target.is_file():
            continue
        url = URL_TEMPLATE.format(ref=args.ref, header=header)
        try:
            with urlopen(url, timeout=TIMEOUT) as response:  # ruff: ignore[suspicious-url-open-usage]
                target.write_bytes(response.read())
        except OSError as e:
            print(f'Failed to download {url}: {e}', file=sys.stderr)  # ruff: ignore[print]
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
