#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys

from normalize import trim_comments_and_redundant_whitespace


def main():
    parser = argparse.ArgumentParser('tests for //build/fidl/normalize.py')
    parser.add_argument('--stamp', help='Path to the stamp file', required=True)
    args = parser.parse_args()

    cases = [
        {
            'input': '//',
            'expected': '',
        }, {
            'input': '',
            'expected': '',
        }, {
            'input': 'a//',
            'expected': 'a\n',
        }, {
            'input': ' a//',
            'expected': 'a\n',
        }, {
            'input': ' \t a \t\t    b  //  c\t',
            'expected': 'a b\n',
        }
    ]
    for case in cases:
        source = case['input']
        expected = case['expected']
        actual = trim_comments_and_redundant_whitespace(source)
        if expected != actual:
            print(source, expected, actual)
            sys.exit(1)

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('done')
    return 0


if __name__ == '__main__':
    sys.exit(main())
