# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


from functools import reduce
from typing import List, Iterable


class RangeSet:
    """Collection of discrete integer ranges.

    This implementation is append-only. It uses Python's built-in `range`, so
    all the ranges are [closed, open).
    """
    ranges: List[range]

    def __init__(self, ranges: Iterable[range] = None):
        """Constructs an empty RangeSet.
        Args:
          ranges: For internal use only.
        """
        self.ranges = list(ranges) if ranges else []

    def __len__(self):
        return reduce(lambda x, y: x + y, map(len, self.ranges))

    def __eq__(self, other):
        return self.ranges == other.ranges

    def append(self, code_point: int) -> None:
        """Appends an integer to the end of the set."""
        if self.ranges:
            last_range = self.ranges[-1]
            if code_point <= last_range[-1]:
                raise IndexError('Can\'t append to the middle of the range set')
            if last_range[-1] + 1 == code_point:
                self.ranges[-1] = range(last_range[0], code_point + 1)
                return
        self.ranges.append(range(code_point, code_point + 1))

    def to_offset_string(self) -> str:
        """Generate a compact string representation of the RangeSet.

        If the set of ranges is

            [range(1,4), range(8,10), range(13, 14), range(18, 21)]

        the output will be

            "1+2,5+1,4,4+3"

        In each entry, the first number is the offset from the end of the
        previous range. If the current range has length > 1, then there's a
        '+x' that shows how much to add to get the upper bound of the range.
        Note that the range [13, 14) has length 1, so it doesn't get a plus
        suffix.
        """
        prev_start = 0
        segments = []
        for r in self.ranges:
            relative_start = r[0] - prev_start
            range_size = len(r)
            if range_size == 1:
                segments.append('%d' % relative_start)
            else:
                segments.append('%d+%d' % (relative_start, range_size - 1))
            prev_start = r[-1]
        return ','.join(segments)

    @classmethod
    def from_offset_string(cls, offsets: str) -> 'RangeSet':
        """Construct a RangeSet from an offset string.

        See `to_offset_string` for a description of the format.
        """
        ranges = []
        split_offsets = offsets.split(',')
        last_end = 0
        for s in split_offsets:
            if '+' in s:
                endpoints = s.split('+')
                start = last_end + int(endpoints[0])
                end = start + int(endpoints[1])
                ranges.append(range(start, end + 1))
                last_end = end
            else:
                start = last_end + int(s)
                ranges.append(range(start, start + 1))
                last_end = start
        return RangeSet(ranges=ranges)
