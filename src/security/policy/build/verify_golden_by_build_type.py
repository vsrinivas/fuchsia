#!/usr/bin/env python3.8

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import json
import sys

from pathlib import Path


def main():
    params = argparse.ArgumentParser(
        description=
        "Verify file against golden with different goldens for eng and non-eng build types"
    )
    params.add_argument("--eng-golden", type=Path, required=False)
    params.add_argument("--non-eng-golden", type=Path, required=True)
    params.add_argument("--product-config", type=Path, required=True)
    params.add_argument("--input", type=Path, required=True)
    params.add_argument("--output", type=Path, required=True)
    args = params.parse_args()

    with open(args.product_config, 'r') as product_config:
        product_config_json = json.load(product_config)
        build_type = product_config_json['platform']['build_type']
        if build_type == 'eng':
            golden = args.eng_golden
        else:
            golden = args.non_eng_golden
        if golden is not None and not filecmp.cmp(golden, args.input,
                                                  shallow=False):
            raise Exception(
                'Golden and input files differ:\ngolden={}\ninput={}'.format(
                    golden, args.input))

    args.output.touch()


if __name__ == "__main__":
    sys.exit(main())
