#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Script to format JSON files.

This script accepts a list of files as arguments, and for each of them attempts
to parse it as JSON, and update it in-place with a pretty-printed version. Stops
on the first error.
"""

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        'file',
        type=argparse.FileType('r+'),
        nargs='+',
        help='JSON file to be pretty-printed.')
    args = parser.parse_args()
    for json_file in args.file:
        try:
            with json_file:
                original = json_file.read()
                data = json.loads(original)
                formatted = json.dumps(
                    data, indent=4, sort_keys=True, separators=(',', ': '))
                if original != formatted:
                    json_file.seek(0)
                    json_file.truncate()
                    json_file.write(formatted + '\n')
        except:
            print(
                "Exception encountered while processing file " + json_file.name)
            raise


if __name__ == "__main__":
    main()
