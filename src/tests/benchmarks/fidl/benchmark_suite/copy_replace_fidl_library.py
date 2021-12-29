#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generates a fidl file that is a copy of a fidl file but with the library
# name replaced with the name passed to this script.
#
# This exists because benchmark FIDL files are sometimes used in a single
# fidl() GN build rule and sometimes in multiple GN build rules and the
# library name can't be reused across build rules.

import sys


def main():
    if len(sys.argv) != 4:
        print(
            "expected 3 arguments: [input file] [output file] [new fidl library]",
            file=sys.stderr,
        )
        return 2

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    new_fidl_library = sys.argv[3]

    with open(input_file) as fi:
        with open(output_file, "w") as fo:
            for line in fi:
                if 'library test.benchmarkfidl;\n' in line:
                    fo.write('library %s;\n' % new_fidl_library)
                else:
                    fo.write(line)


if __name__ == "__main__":
    sys.exit(main())
