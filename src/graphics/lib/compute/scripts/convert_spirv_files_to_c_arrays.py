#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Turn one or more compiled SPIR-V shaders into C builtin-arrays."""
from __future__ import print_function

import os
import argparse
import struct
import sys


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input_file', nargs='+', default=[])
    parser.add_argument('--output', help='Optional output file path.')

    args = parser.parse_args(argv)

    if not args.output:
        output = sys.stdout
    else:
        output = open(args.output, 'wt')

    for input_file in args.input_file:
        if not os.path.exists(input_file):
            sys.stderr.write('ERROR: Missing file: %s' % input_file)
            sys.exit(1)

        with open(input_file, "rb") as f:
            input_data = f.read()
        if len(input_data) & 3 != 0:
            sys.stderr.write(
                'ERROR: Length of file (%d) is not multiple of 4: %s' %
                (len(input_data), input_file))
            sys.exit(1)

        file_name = os.path.basename(input_file)
        for extension in ('.spv', '.glsl'):
            ext_len = len(extension)
            if len(file_name) > ext_len and file_name[-ext_len:] == extension:
                file_name = file_name[:-ext_len]
                break

        array_name = file_name.replace('.', '_')
        output.write('// Auto-generated - DO NOT TOUCH!\n')
        output.write('static const uint32_t %s_data[] = {\n' % array_name)
        n = 0
        total_len = len(input_data)
        while n < total_len:
            available = total_len - n
            count = min(available, 32)
            line = "  "
            comma = ""
            for m in range(0, count, 4):
                line += comma
                line += '0x%08x' % struct.unpack_from('<I', input_data, n + m)
                comma = ', '
            n += count
            line += ',\n'
            output.write(line)
        output.write('};\n\n')

    output.close()


if __name__ == "__main__":
    main(sys.argv[1:])
