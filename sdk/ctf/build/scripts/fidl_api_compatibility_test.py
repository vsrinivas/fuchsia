# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
import filecmp
import os
import sys
import plasa_differ
from enum import Enum

# TODO(kjharland): Write unit tests.


class Policy(Enum):
    # update_golden tells this script to overwrite the existing golden with
    # the current golden. This is useful for batch updates.
    update_golden = 'update_golden'

    # ack_changes tells this script to fail if any changes are detected.
    # It will tell the user how to update the golden to acknowledge the
    # changes.
    ack_changes = 'ack_changes'

    # no_breaking_changes tells this script to fail if breaking changes
    # are detected. If non-breaking changes are detected it will tell
    # the user how to update the golden to acknowledge the changes.
    no_breaking_changes = 'no_breaking_changes'

    # no_changes tells this script to fail if any changes are detected.
    # It will not ask the user to update the golden.
    no_changes = 'no_changes'

    def __str__(self):
        return self.value


FIDL_VERSIONING_DOCS_URL = "https://fuchsia.dev/fuchsia-src/reference/fidl/language/versioning"

FIDL_AVAILABILITY_HINT = (
    f"Are you missing an @available annotation?\n"
    f"For more information see {FIDL_VERSIONING_DOCS_URL}\n")


class CompatibilityError(Exception):
    """Exception raised when breaking API changes are detected"""

    def __init__(self, api_level, breaking_changes, current, golden):
        self.api_level = api_level
        self.breaking_changes = breaking_changes
        self.current = current
        self.golden = golden

    def __str__(self):
        formatted_breaking_changes = '\n - '.join(self.breaking_changes)
        return (
            f"These changes are incompatible with API level {self.api_level}\n"
            f"{formatted_breaking_changes}\n\n"
            f"{FIDL_AVAILABILITY_HINT}")


class GoldenMismatchError(Exception):
    """Exception raised when a stale golden file is detected."""

    def __init__(
            self,
            api_level,
            current,
            golden,
            show_update_hint=False,
            show_fidl_availability_hint=True):
        self.api_level = api_level
        self.current = current
        self.golden = golden
        self.show_update_hint = show_update_hint
        self.show_fidl_availability_hint = show_fidl_availability_hint

    def __str__(self):
        hints = []
        if self.show_fidl_availability_hint:
            hints.append(FIDL_AVAILABILITY_HINT)

        if self.show_update_hint:
            cmd = update_cmd(self.current, self.golden)
            hints.append(
                f"Please acknowledge this change by updating the golden.\n"
                f"To do this, please run:\n"
                f"  {cmd}\n")

        hint_lines = '\n'.join(hints)
        return f"Detected changes to API level {self.api_level}\n" + hint_lines


def golden_not_found_error(filename):
    message = (
        f"The golden file {filename} does not exist.\n"
        f"If this is a new FIDL API you must first create this file.\n"
        f"To do so, run:\n"
        f"  touch {os.path.abspath(filename)}\n")
    return FileNotFoundError(errno.ENOENT, message, filename)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--policy',
        help="How to handle failures",
        type=Policy,
        default=Policy.no_breaking_changes,
        choices=list(Policy))
    parser.add_argument(
        '--depfile', help='Where to write the depfile', required=True)
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

    dependencies = [args.current, args.golden]

    if not os.path.exists(args.golden):
        if os.path.getsize(args.current) > 0:
            err = golden_not_found_error(args.golden)
        else:
            # Don't depend on files that don't exist.
            dependencies.remove(args.golden)

            # Skip testing if current is empty and golden is missing.
            # This prevents us from asking users to create empty golden files.
            err = None
    elif args.policy == Policy.update_golden:
        err = update_golden(args)
    elif args.policy == Policy.no_breaking_changes:
        err = fail_on_breaking_changes(args)
    elif args.policy == Policy.no_changes:
        err = fail_on_changes(args)
    elif args.policy == Policy.ack_changes:
        err = fail_on_unacknowledged_changes(args)
    else:
        raise ValueError("unknown policy: {}".format(args.policy))

    with open(args.depfile, 'w') as f:
        f.write("{}: {}\n".format(args.stamp, ' '.join(dependencies)))

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    if not err:
        return 0

    print("\nERROR: ", err)
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

    if not filecmp.cmp(args.golden, args.current):
        return GoldenMismatchError(
            api_level=args.api_level,
            current=args.current,
            golden=args.golden,
            show_update_hint=True,
            # No breaking changes so the user must be using `@available` correctly.
            show_fidl_availability_hint=False,
        )

    return None


def fail_on_unacknowledged_changes(args):
    """Asks the user to fix the golden if current and golden aren't identical."""
    if not filecmp.cmp(args.golden, args.current):
        return GoldenMismatchError(
            api_level=args.api_level,
            current=args.current,
            golden=args.golden,
            show_update_hint=True,
        )
    return None


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
