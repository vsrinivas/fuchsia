#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(  # scripts
        SCRIPT_DIR))  # tests


# Stdin: lines of URLs of tests
# Stdout: lines of corresponding GN labels (empty line for no match)
# Assumes that you called `fx set ...`
def main():
    with open(os.path.join(FUCHSIA_ROOT,
                           'out/default/tests.json')) as json_file:
        tests_json = json.loads(json_file.read())
        url_to_label = {
            entry['test']['name']: entry['test']['label']
            for entry in tests_json
        }
        for line in sys.stdin:
            print(url_to_label.get(line.strip(), ''))

    return 0


if __name__ == '__main__':
    sys.exit(main())
