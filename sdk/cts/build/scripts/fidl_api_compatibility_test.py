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


class CompatibilityError(Exception):
    """Exception raised when breaking API changes are detected"""

    def __init__(self, api_level, breaking_changes, current, golden):
        self.api_level = api_level
        self.breaking_changes = breaking_changes
        self.current = current
        self.golden = golden

    def __str__(self):
        formatted_breaking_changes = '\n - '.join(breaking_changes)
        cmd = update_cmd(self.current, self.golden)
        return (
            f"These changes are incompatible with API level {self.api_level}\n"
            f"{formatted_breaking_changes}\n\n"
            f"If possible, please make a soft transition instead.\n"
            f"To allow a hard transition please run:\n"
            f"  {cmd}")


class GoldenMismatchError(Exception):
    """Exception raised when a stale golden file is detected."""

    def __init__(self, api_level, current, golden):
        self.api_level = api_level
        self.current = current
        self.golden = golden

    def __str__(self):
        cmd = update_cmd(self.current, self.golden)
        return (
            f"Detected changes to API level {self.api_level}\n"
            f"Please acknowledge this change by updating the golden.\n"
            f"You can rebuild with `--args=bless_goldens=true` or run:\n"
            f"  {cmd}")


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
    parser.add_argument(
        '--warn_on_changes',
        help='Treat compatibility violations as warnings',
        action='store_true')
    args = parser.parse_args()

    if args.policy == Policy.update_golden:
        err = update_golden(args)
    elif args.policy == Policy.no_breaking_changes:
        err = fail_on_breaking_changes(args)
    elif args.policy == Policy.no_changes:
        err = fail_on_changes(args)
    elif args.policy == Policy.ack_changes:
        err = fail_on_changes(args)
    else:
        raise ValueError("unknown policy: {}".format(args.policy))

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    if not err:
        return 0

    if args.warn_on_changes:
        print("WARNING: ", err)
        return 0

    print("ERROR: ", err)
    return 1


def update_golden(args):
    import subprocess
    c = update_cmd(args.current, args.golden)
    subprocess.run(c.split())
    return None


def fail_on_breaking_changes(args):
    """Fails if current is not backward compatible with golden or if
    current and golden aren't identical.
    """
    differ = plasa_differ.PlasaDiffer(args.fidl_api_diff_path)
    breaking_changes = differ.find_breaking_changes_in_fragment_file(
        args.golden, args.current)

    if breaking_changes:
        return CompatibilityError(
            api_level=args.api_level,
            breaking_changes=breaking_changes,
            current=args.current,
            golden=args.golden,
        )

    # Make the developer acknowledge a stale golden file.
    return fail_on_changes(args)


def fail_on_changes(args):
    """Fails if current and golden aren't identical."""
    if not filecmp.cmp(args.golden, args.current):
        return GoldenMismatchError(
            api_level=args.api_level,
            current=args.current,
            golden=args.golden,
        )
    return None


def update_cmd(current, golden):
    return "cp {} {}".format(os.path.abspath(current), os.path.abspath(golden))


if __name__ == '__main__':
    sys.exit(main())
