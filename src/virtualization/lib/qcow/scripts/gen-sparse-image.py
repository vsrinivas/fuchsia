#!/usr/bin/env python3
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
A helper script to generate sparse files that contain some written sectors. These raw disk images
will be converted to qcow images by qemu-img for testing.
"""

import optparse
import re
import sys


# This largely matches the number formats accepted by qemu-img.
def to_multiplier(c):
    multipler = 1
    if not c:
        return 1
    multipler *= 1024
    if c == 'k' or c == 'K':
        return multipler
    multipler *= 1024
    if c == 'M':
        return multipler
    multipler *= 1024
    if c == 'G':
        return multipler
    multipler *= 1024
    if c == 'T':
        return multipler
    multipler *= 1024
    if c == 'P':
        return multipler
    multipler *= 1024
    if c == 'E':
        return multipler
    raise ValueError(f'Unsupported size suffix "{c}"')


# Takes a size string (ex: 1k) and converts it into a integer (ex: 1024).
def resolve_size(size):
    m = re.match('([0-9]+)([kKMGTPE]?)', size)
    if not m:
        return None
    return int(m[1]) * to_multiplier(m[2])


def gen_image(filename, file_size, writes):
    file = open(filename, 'wb')
    # Truncate to the full size
    file.truncate(file_size)
    for write in writes:
        (offset, size, value) = write
        if offset + size > file_size:
            raise ValueError("Out of Bounds")
        file.seek(offset)
        data = bytes([value] * size)
        file.write(data)


def main():
    parser = optparse.OptionParser()
    parser.add_option(
        '-f', '--file', dest='filename', help='name of the output file')
    parser.add_option(
        '-s',
        '--size',
        dest='size',
        help=
        'The size of the file to create. This can be in bytes or it can accept suffixes for '
        'Kib (k or K), Mib (M), GiB (G), TiB (T), EiB (E), or PiB (P). Ex: "--size=256M" would '
        'create a 256 MiB file.')
    parser.add_option(
        '-w',
        '--write',
        dest='write',
        action='append',
        help=
        'Write a region of the sparse file. This takes an argument of the form '
        '"<offset>+<length>=<byte>" where "offset" and "length" are byte values and can take '
        'the same suffixes as the --size argument. The "byte" value must be 0 <= byte < 256 '
        'and can be in hex or decimal format. For example --write="1M+2k=3" would populate a 2 KiB '
        'region in the file, starting 1 MiB into the file with all bytes set to 3.'
    )

    options, _ = parser.parse_args()
    if not options.filename:
        parser.error('Missing required "--filename" option')
    if not options.size:
        parser.error('Missing required "--size" option')
    size = resolve_size(options.size)
    if size is None:
        parser.error('Invalid "--size" option')

    writes = []
    if options.write is not None:
        for spec in options.write:
            m = re.match('(\w+)\+(\w+)=(0x[0-9a-fA-F]+|\w+)', spec)
            if not m:
                raise ValueError('invalid option!')
                break
            offset = resolve_size(m[1])
            length = resolve_size(m[2])
            pattern = int(m[3], 0)
            if pattern > 255:
                raise ValueError('write value must be a single byte')
            writes.append((offset, length, pattern))

    gen_image(options.filename, size, writes)


if __name__ == '__main__':
    sys.exit(main())
