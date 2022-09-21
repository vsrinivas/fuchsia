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
        "Verify that non-eng build types have a specified structured configuration policy."
    )
    params.add_argument("--product-config", type=Path, required=True)
    params.add_argument("--structured-config-policy", type=Path)
    params.add_argument("--output", type=Path, required=True)
    args = params.parse_args()

    with open(args.product_config, 'r') as product_config:
        product_config_json = json.load(product_config)
        build_type = product_config_json['platform']['build_type']
        if build_type != 'eng':
            if args.structured_config_policy is None or not args.structured_config_policy.is_file(
            ):
                print(
                    "Non-eng builds must specify a security policy for structured config."
                )
                return 1

    args.output.touch()


if __name__ == "__main__":
    sys.exit(main())
