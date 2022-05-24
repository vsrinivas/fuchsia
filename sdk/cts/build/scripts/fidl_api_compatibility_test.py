# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import json
import os
import sys
import plasa_differ
from enum import Enum


class Policy(Enum):
    # update_golden tells this script to overwrite the existing golden with
    # the current golden. This is useful for batch updates.
    update_golden = 'update_golden'

    # ack_changes is the same as 'no_changes' but communicates the intent
    # better in places where this script is called.
    ack_changes = 'ack_changes'

    # no_breaking_changes tells this script to fail if breaking changes
    # are detected, or if any changes are detected to prevent a stale
    # golden.
    no_breaking_changes = 'no_breaking_changes'

    # no_changes tells this script to fail if any changes are detected.
    # It will tell the user how to update the golden to acknowledge the
    # changes.
    no_changes = 'no_changes'

    def __str__(self):
        return self.value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--policy',
        help="How to handle failures",
        type=Policy,
        default=Policy.no_breaking_changes,
        choices=list(Policy))
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

    ret = 0
    if args.policy == Policy.update_golden:
        ret = update_golden(args)
    elif args.policy == Policy.no_breaking_changes:
        ret = fail_on_breaking_changes(args)
    elif args.policy == Policy.no_changes:
        ret = fail_on_changes(args)
    elif args.policy == Policy.ack_changes:
        ret = fail_on_changes(args)
    else:
        raise ValueError("unknown policy: {}".format(args.policy))

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    return ret


def update_golden(args):
    import subprocess
    c = update_cmd(args.current, args.golden)
    subprocess.run(c.split())
    return 0


def fail_on_breaking_changes(args):
    """Fails if current is not backward compatible with golden or if
    current and golden aren't identical.
    """
    differ = plasa_differ.PlasaDiffer(args.fidl_api_diff_path)
    breaking_changes = differ.find_breaking_changes_in_fragment_file(
        args.golden, args.current)

    if breaking_changes:
        print("These changes are incompatible with API level " + args.api_level)
        for breaking_change in breaking_changes:
            print(" - " + breaking_change)
        print()
        print("If possible, please make a soft transition instead.")
        print("To allow a hard transition please run:")
        print("  " + update_cmd(args.current, args.golden))
        print()
        return 1

    # Make the developer acknowledge a stale golden file.
    return fail_on_changes(args)


def fail_on_changes(args):
    """Fails if current and golden aren't identical."""
    if not filecmp.cmp(args.golden, args.current):
        print("Error: Detected changes to API level {}".format(args.api_level))
        print("Please acknowledge this change by updating the golden.\n")
        print("You can rebuild with `bless_goldens=true` in your GN args,")
        print("or you can run this command:")
        print("  " + update_cmd(args.current, args.golden))
        return 1
    return 0


def update_cmd(current, golden):
    return "cp {} {}".format(os.path.abspath(current), os.path.abspath(golden))


if __name__ == '__main__':
    sys.exit(main())
