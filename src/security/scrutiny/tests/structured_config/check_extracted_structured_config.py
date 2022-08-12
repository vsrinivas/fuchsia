# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import pathlib
import unittest


def main():
    parser = argparse.ArgumentParser(
        description="Check the report of configuration produced by scrutiny.")
    parser.add_argument(
        "--extracted-config",
        type=pathlib.Path,
        required=True,
        help="Path to JSON dump of structured configuration from scrutiny.")
    parser.add_argument(
        "--expected-url",
        type=str,
        required=True,
        help="URL of the component whose configuration we're asserting.")
    parser.add_argument(
        "--expected-key",
        type=str,
        required=True,
        help="Expected name of configuration key.")
    parser.add_argument(
        "--expected-value",
        type=str,
        required=True,
        help="Expected value to find in the JSON for this component.")
    args = parser.parse_args()

    test = unittest.TestCase()

    with open(args.extracted_config) as f:
        extracted_config = json.load(f)

    test.assertEqual(
        extracted_config[args.expected_url],
        {args.expected_key: args.expected_value},
        "configuration from system image did not match expectation")
