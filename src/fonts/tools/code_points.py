#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Get code point coverage for a font file.

The default output is in a compact format that expresses the ranges of code
points as a series of offsets from the end of the previous range. For
example, if the file contains the code points

    [1, 2, 3, 8, 9, 13, 17, 18, 19, 20]

the set of ranges is

   [[1,3], [8,9], [13, 13], [17, 20]]

and the output will be

    "1+2,5+1,4,4+3"

In each entry, the first number is the offset from the end of the previous
range. If the current range has length > 1, then there's a '+x' that shows how
much to add to get the upper bound of the range. Note that the range [13, 13]
has length 1, so it doesn't get a plus suffix.
"""

import os
import sys

# Add fuchsia/third_party/fonttools/Lib to the path.
font_tools_path = os.path.normpath(
    os.path.join(os.path.realpath(__file__), '..', '..', '..', '..',
                 'third_party', 'fonttools', 'Lib'))
sys.path.insert(0, font_tools_path)

import argparse
from typing import NamedTuple, Dict

from fontTools import ttLib

from range_set import RangeSet


class AssetReference(NamedTuple):
    """Reference to font asset, with file path and optional index into the file.
    """
    path: str
    index: int = 0


class FontInfoReader:
    """Wrapper around ttlib.TTFont for reading font files.
    """

    def open_font(self, asset: AssetReference) -> ttLib.TTFont:
        """Opens a font file, optionally with a specific font index."""
        if asset.index is not None:
            return ttLib.TTFont(asset.path, fontNumber=asset.index)
        else:
            return ttLib.TTFont(asset.path)

    def get_best_cmap(self, asset: AssetReference) -> Dict:
        """Gets the best character map table (cmap) from the font file.

        Cf. https://docs.microsoft.com/en-us/typography/opentype/spec/cmap
        """
        ttfont = self.open_font(asset)
        return ttfont.getBestCmap()

    def get_ranges(self, asset: AssetReference) -> RangeSet:
        """Get the set of code points covered by the given asset."""
        cmap = self.get_best_cmap(asset)
        range_set = RangeSet()
        for k, _ in cmap.items():
            range_set.append(k)
        return range_set


def main():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.description = __doc__
    parser.add_argument('path', type=str, help='Path to the font file')
    parser.add_argument('-i', '--index', type=int, default=None,
                        help=(
                            'For font files that contain collections of fonts '
                            '(.ttc), you must supply an index'))
    parser.add_argument('-m', '--metadata', action='store_true',
                        help=(
                            'Instead of the code point offset string, '
                            'print font metadata.'))
    args = parser.parse_args()

    reader = FontInfoReader()
    asset = AssetReference(args.path, args.index)
    range_set = reader.get_ranges(asset)
    as_offset_string = range_set.to_offset_string()

    if args.metadata:
        print('')
        print('Code points:\n\t%d' % len(range_set))
        print('Offset string length:\n\t%d' % len(as_offset_string))
        print('')
    else:
        print(as_offset_string)
    pass


if __name__ == '__main__':
    main()
