#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generates a rust file that combines multiple GIDL-generated benchmarks that export a
# BENCHMARK array of benchmark functions into an ALL_BENCHMARKS array that is used by
# src/tests/benchmarks/fidl/rust/src/main.rs.

# Example output:
# use fuchsia_criterion::criterion::Bencher;
#
# use benchmarkfidltable;
# use benchmarkfidlunion;
#
# pub const ALL_BENCHMARKS: [&\'static[(&\'static str, fn(&mut Bencher))]; %d] = [
#  &benchmarkfidltable::BENCHMARKS,
#  &benchmarkfidlunion::BENCHMARKS,
# ];

import sys


def main():
    if len(sys.argv) < 2:
        print(
            'expect at least 1 argument: [output file] ([package name])*',
            file=sys.stderr,
        )
        return 2

    output_file = sys.argv[1]
    packages = sys.argv[2:]

    with open(output_file, "w") as fo:
        fo.write('use fuchsia_criterion::criterion::Bencher;\n\n')
        for package in packages:
            fo.write('use %s;\n' % package)
        fo.write(
            '\npub const ALL_BENCHMARKS: [&\'static[(&\'static str, fn(&mut Bencher))]; %d] = [\n'
            % len(packages))
        for package in packages:
            fo.write('\t&%s::BENCHMARKS,\n' % package)
        fo.write('];\n')


if __name__ == "__main__":
    sys.exit(main())
