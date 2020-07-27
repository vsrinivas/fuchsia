#!/usr/bin/env python3.8
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generates the packages.json file in the update package.

import json
import sys


def main(input_path, output_path):
    with open(input_path, 'r') as input_file:
        pkgurls = []
        for line in input_file:
            package, merkle_path = line.strip().split('=', 1)
            merkle = open(merkle_path, 'r').read().strip()
            pkgurls += [
                'fuchsia-pkg://fuchsia.com/{}?hash={}'.format(package, merkle)
            ]
        with open(output_path, 'w') as output_file:
            packages_json = {'version': 1, 'content': pkgurls}
            output_file.write(json.dumps(packages_json))


if __name__ == '__main__':
    main(*sys.argv[1:])
