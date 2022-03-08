# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Read two gerrit sizes report and fails with error
when the values differ more than the hard coded tolerances.
'''
import argparse
import json
import sys
import itertools


def is_different(key, expected, actual):
    """Compares the actual value to the expected one.

  Args:
    key: string, name of the size report dictionary key.
    expected: value used as reference to check the actual.
    actual: value to be verified.

  Returns:
    True when the actual value is out of tolerance.
  """
    # Ignores small differences in url escaping.
    if key.endswith(".owner"):
        actual = actual.replace("%3A", ":")
        return expected != actual

    # Amount of difference between the value beyond which one the test fails.
    accepted_difference = {
        "/system (drivers and early boot)": 200000,
        # See fxb/94824#c7: The new tool uses blobfs rather than blobfs-compress
        # which is more efficient and return smaller files.
        "Update Package": 100000,
        # The new tool use the same rounding strategy for resources and packages.
        # The former tool makes exact computation for resource and rounding for packages.
        "Distributed shared libraries": 500,
        "ICU Data": 20,
    }
    return abs(expected - actual) > accepted_difference.get(key, 0)


def main():
    parser = argparse.ArgumentParser(
        description='Compares size reports with hardcoded tolerances.')
    parser.add_argument(
        '--expected', type=argparse.FileType('r'), required=True)
    parser.add_argument('--actual', type=argparse.FileType('r'), required=True)
    parser.add_argument('--output', type=argparse.FileType('w'), required=True)
    args = parser.parse_args()

    # Load JSON size reports.
    expected = json.load(args.expected)
    actual = json.load(args.actual)

    # Compare the files key by key.
    errors = []
    for key in sorted(set(itertools.chain(expected, actual))):
        if key not in expected:
            errors.append(f"ERROR: extraneous key: {key}")
        elif key not in actual:
            errors.append(f"ERROR: missing key key: {key}")
        elif is_different(key, expected[key], actual[key]):
            errors.append(f"ERROR: different value for key: {key}")
            errors.append(f"  Expected: {expected[key]}")
            errors.append(f"  Actual:   {actual[key]}")

    # Write output to file and stdout.
    print("\n".join(errors))
    args.output.write("\n".join(errors))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
