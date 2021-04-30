#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import re
import subprocess
import sys


def extract_field(line, regex, name, fields):
    result = regex.match(line)
    if result:
        fields[name] = result.group(1)


def main():
    parser = argparse.ArgumentParser(
        description='Extract the descriptor fields from a vbmeta image')
    parser.add_argument('--avbtool', required=True)
    parser.add_argument('--vbmeta', required=True)
    parser.add_argument('--output', type=argparse.FileType('w'))
    parser.add_argument('--salt-only', action='store_true', default=False)
    args = parser.parse_args()

    avb_output = subprocess.check_output(
        [args.avbtool, 'info_image', '--image', args.vbmeta],
        universal_newlines=True).splitlines()

    min_version_re = re.compile('.*Minimum libavb version:\s*([0-9\.]+)')
    size_re = re.compile('.*Image Size:\s*([0-9]+) bytes')
    name_re = re.compile('.*Partition Name:\s*(\w+)')
    salt_re = re.compile('.*Salt:\s*([0-9a-f]+)')
    digest_re = re.compile('.*Digest:\s*([0-9a-f]+)')
    flags_re = re.compile('.*Flags:\s*([0-9a-f]+)')

    fields = {}
    for line in avb_output:
        extract_field(line, min_version_re, "min_avb_version", fields)
        extract_field(line, size_re, "size", fields)
        extract_field(line, name_re, "name", fields)
        extract_field(line, salt_re, "salt", fields)
        extract_field(line, digest_re, "digest", fields)
        extract_field(line, flags_re, "flags", fields)

    if args.salt_only:
        if "salt" in fields:
            output = fields["salt"]
        else:
            return -1
    else:
        output = json.dumps(fields, indent=2)

    if args.output is not None:
        args.output.write(output)
    else:
        print(output)


if __name__ == '__main__':
    sys.exit(main())
