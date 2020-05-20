#!/usr/bin/env python3

import argparse
import collections
import enum
import subprocess
import sys

import regen
import test_
import util

EXAMPLES = """
Examples:

Regen goldens and checked in bindings based on changed files in the current
repo

    fidldev regen

Explicitly specify regen scripts:

    fidldev regen fidlc fidlgen_dart
    fidldev regen all

Check which regen commands should be run:

    fidldev regen --dry-run --no-build

Run tests based on changed files in the current repo:

    fidldev test

Explicitly specify tests:

    fidldev test fidlc hlcpp llcpp c

Interactively filter test targets:

    fidldev test --interactive

Check which tests should be run:

    fidldev test --dry-run --no-build --no-regen

Pass flags to invocations of fx test:

    fidldev test --fx-test-args "-v -o --dry"

"""


def test(args):
    if args.targets:
        if not args.no_regen:
            util.print_warning(
                'explicit test targets provided, skipping regen...')
        test_.test_explicit(
            args.targets, not args.no_build, args.dry_run, args.interactive,
            args.fx_test_args)
    else:
        changed_files = util.get_changed_files()
        if not args.no_regen:
            regen.regen_changed(changed_files, not args.no_build, args.dry_run)
            changed_files = util.get_changed_files()
        test_.test_changed(
            changed_files, not args.no_build, args.dry_run, args.interactive,
            args.fx_test_args)
        if args.dry_run:
            print_dryrun_warning()


def regen_cmd(args):
    if args.targets:
        regen.regen_explicit(args.targets, not args.no_build, args.dry_run)
    else:
        changed_files = util.get_changed_files()
        regen.regen_changed(changed_files, not args.no_build, args.dry_run)
        if args.dry_run:
            print_dryrun_warning()


def print_dryrun_warning():
    print(
        'NOTE: dry run is conservative and assumes that regen will '
        'always change files. If goldens do not change during an actual '
        'run, fewer tests/regen scripts may be run.')


parser = argparse.ArgumentParser(
    description="FIDL development workflow tool",
    formatter_class=argparse.RawDescriptionHelpFormatter,
    epilog=EXAMPLES)
subparsers = parser.add_subparsers()

test_parser = subparsers.add_parser("test", help="Test your FIDL changes")
test_parser.set_defaults(func=test)
test_targets = [name for (name, _) in test_.TEST_GROUPS] + ['all']
test_parser.add_argument(
    'targets',
    metavar='target',
    nargs='*',
    help=
    "Manually specify targets to regen, where a target is one of {}. Omit positional arguments to test based on changed files"
    .format(test_targets))
test_parser.add_argument(
    "--dry-run",
    "-n",
    help="Print out test commands without running",
    action="store_true",
)
test_parser.add_argument(
    "--no-build",
    "-b",
    help="Don't rebuild targets used for testing",
    action="store_true",
)
test_parser.add_argument(
    "--no-regen",
    "-r",
    help="Don't regen goldens before running tests",
    action="store_true",
)
test_parser.add_argument(
    "--interactive",
    "-i",
    help="Interactively filter tests to be run",
    action="store_true",
)
test_parser.add_argument(
    "--fx-test-args",
    "-t",
    help=
    "Extra flags and arguments to pass to any invocations of fx test. The flag value is passed verbatim. By default, only '-v' is used.",
    default='-v',
)

regen_parser = subparsers.add_parser("regen", help="Run regen commands")
regen_parser.set_defaults(func=regen_cmd)
regen_targets = [name for (name, _) in regen.REGEN_TARGETS] + ['all']
regen_parser.add_argument(
    'targets',
    metavar='target',
    nargs='*',
    help=
    "Manually specify targets to regen, where a target is one of {}. Omit positional arguments to regen based on changed files"
    .format(regen_targets))
regen_parser.add_argument(
    "--dry-run",
    "-n",
    help="Print out commands without running them",
    action="store_true",
)
regen_parser.add_argument(
    "--no-build",
    "-b",
    help="Don't rebuild targets used for regen",
    action="store_true",
)

if __name__ == '__main__':
    args = parser.parse_args()
    args.func(args)
