#!/usr/bin/env python3

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Converts an image file to RLE format.

This is not a general-purpose tool, but is specifically intended to convert
Fuchsia logo files from common image formats (e.g. PNG) to a simple custom
RLE format understood by Gigaboot.
"""

import argparse
import logging
import os
import struct
import sys
from typing import Tuple

from PIL import Image

# Logo color as of 2021-07-13. Alpha channel gives brightness.
_FUCHSIA_LOGO_COLOR = (241, 243, 244)

# Log the pixel values at some indices to test our decoder with. Values
# chosen to try to get a few different values plus the endpoints.
_TEST_INDICES = (0, 60140, 90547, 91905, 179305, (512 * 512 - 1))


class Rle:
    """RLE encoder.

    Input data must be a single byte, output format will be a sequence of
    (repeat, value) byte pairs. Total copes of the value is (repeat + 1).
    """

    class Entry():

        def __init__(self, value):
            self.repeat = 0
            self.value = value

    def __init__(self):
        self.entries = []
        self.count = 0

    def add_value(self, value: int):
        """Adds the next value."""
        if not 0 <= value <= 0xFF:
            raise ValueError(f"Out-of-bounds RLE value: {value}")

        if (self.entries and self.entries[-1].value == value and
                self.entries[-1].repeat < 0xFF):
            self.entries[-1].repeat += 1
        else:
            self.entries.append(Rle.Entry(value))

        if self.count in _TEST_INDICES:
            rgb = [c * value // 255 for c in _FUCHSIA_LOGO_COLOR]
            logging.info(f"Test index {self.count} = {value}, RGB = {rgb}")

        self.count += 1

    def get_data(self) -> bytes:
        """Returns the RLE data."""
        return b"".join(
            [struct.pack("BB", e.repeat, e.value) for e in self.entries])


def color_to_rle(color: Tuple[int, int, int, int]) -> int:
    """Converts an RGBA color to RLE color.

    Raises:
        ValueError if this color cannot be converted.
    """
    rgb = color[:3]
    if rgb == (0, 0, 0):
        return 0
    elif rgb == _FUCHSIA_LOGO_COLOR:
        # When we match the hardcoded color, the alpha becomes the value.
        return color[3]
    else:
        raise ValueError(f"Pixel has unsupported color {color}")


def convert_to_rle(path: str) -> bytes:
    """Converts an image file to RLE.

    The image format is specific to the Fuchsia logo, and is expected to
    be RGBA where RGB is always either (0,0,0) or _FUCHSIA_LOGO_COLOR.

    For now RLE format is always 1 byte repeat + 1 byte greyscale.

    Args:
        path: path to image file.

    Returns:
        RLE data.

    Raises:
        ValueError if we failed to convert to RLE.
    """
    rle = Rle()

    with Image.open(path) as image:
        if image.mode != "RGBA":
            raise ValueError(f"Image mode {image.mode} is not supported")

        for y in range(image.height):
            for x in range(image.width):
                rle.add_value(color_to_rle(image.getpixel((x, y))))

    # Double-check that we encoded the expected number of pixels.
    pixels = image.height * image.width
    if rle.count != pixels:
        raise RuntimeError(
            f"Internal error: input has {pixels} pixels,"
            f" but RLE encoded {rle.count}")

    return rle.get_data()


def _parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("image", help="path to image file")
    parser.add_argument("out", help="path to output file")

    return parser.parse_args()


def main():
    logging.basicConfig(
        format="\x1b[0;36m%(levelname)s: %(message)s\x1b[0m",
        level=logging.INFO)
    args = _parse_args()

    with open(args.out, "wb") as out_file:
        out_file.write(convert_to_rle(args.image))

    original_size = os.stat(args.image).st_size
    rle_size = os.stat(args.out).st_size
    logging.info(f"Original: {original_size} bytes")
    logging.info(f"RLE: {rle_size} bytes ({rle_size*100//original_size}%)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
