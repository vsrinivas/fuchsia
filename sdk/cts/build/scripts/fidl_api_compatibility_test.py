# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import json
import os
import sys
import plasa_differ


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--update_golden',
        help="Overwrites current with golden if they don't match.",
        action='store_true')
    parser.add_argument(
        '--api-level', help='The API level being tested', required=True)
    parser.add_argument(
        '--golden', help='Path to the golden file', required=True)
    parser.add_argument(
        '--current', help='Path to the local file', required=True)
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    parser.add_argument(
        '--fidl_api_diff_path',
        help='Path to the fidl_api_diff binary',
        required=True)
    args = parser.parse_args()

    differ = plasa_differ.PlasaDiffer(args.fidl_api_diff_path)
    breaking_changes = differ.find_breaking_changes_in_fragment_file(
        args.golden, args.current)

    if breaking_changes:
        update_cmd = "  cp {} {}".format(
            os.path.abspath(args.current), os.path.abspath(args.golden))
        if args.update_golden:
            import subprocess
            subprocess.run(update_cmd.split())
        else:
            print(
                "These changes are incompatible with API level {}:".format(
                    args.api_level))
            for breaking_change in breaking_changes:
                print(" - " + breaking_change)
            print()
            print("If possible, please make a soft transition instead.")
            print("To allow a hard transition please run:")
            print(update_cmd)
            print()
            return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
