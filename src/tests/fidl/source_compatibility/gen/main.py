#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
from pathlib import Path
import os
import signal
import sys

import generate_test
from util import print_err, TEST_FILE
from types_ import CompatTest


def signal_handler(sig, frame):
    print('\nGoodbye')
    sys.exit(0)


def gen_test(args):
    # Users are expected to exit the tool with ^C, so print a message instead of
    # dumping a stacktrace.
    signal.signal(signal.SIGINT, signal_handler)

    test_dir = Path(os.path.join(args.root, args.name))
    os.makedirs(test_dir, exist_ok=True)

    test_file = test_dir / TEST_FILE
    if test_file.exists():
        # This resumes from the latest point. If you'd like to edit existing
        # steps, this must be done manually.
        with open(test_file, 'r') as f:
            test = CompatTest.fromdict(json.load(f))
        state = generate_test.TransitionState.from_test(test)
        generate_test.run(test_dir, state)
    else:
        generate_test.run(test_dir, None)


def regen(args):
    tests = args.tests or [
        p for p in Path(args.root).iterdir()
        if p.is_dir() and (p / TEST_FILE).exists()
    ]
    for name in tests:
        print(f'Regenerating files for {name}')
        test_dir = Path(os.path.join(args.root, name))
        with open(test_dir / TEST_FILE, 'r') as f:
            test = CompatTest.fromdict(json.load(f))
        generate_test.regen_files(test_dir, test)
    if not tests:
        print('No tests found')


parser = argparse.ArgumentParser(
    description="Generate FIDL source compatibility test scaffolding",
    formatter_class=argparse.RawDescriptionHelpFormatter,
    epilog='For full usage details, refer to the tool README.')
parser.add_argument(
    '--root',
    help=
    'Directory that all other paths should be relative to. Useful for testing. Defaults to %(default)s)',
    default=os.path.join(
        os.environ['FUCHSIA_DIR'], 'src/tests/fidl/source_compatibility'),
)
subparsers = parser.add_subparsers()

gen_test_parser = subparsers.add_parser(
    "generate_test", help="Generate a source compatibility test")
gen_test_parser.set_defaults(func=gen_test)
gen_test_parser.add_argument(
    'name',
    help=
    'The test name. Ideally, this should match [parent]-[target]-[change] format specificied in the ABI/API compatibility guide, e.g. protocol-method-add'
)

regen_parser = subparsers.add_parser(
    'regen',
    help=
    "Regenerates the GN sidecar, docs, and BUILD file based on the test's JSON file. Useful when making manual edits."
)
regen_parser.set_defaults(func=regen)
regen_parser.add_argument(
    'tests',
    nargs='*',
    help=
    'Tests to regen (i.e. their paths relative to the --root, like "protocol-method-add"). Tries to regen all tests in the --root directory if none are provided'
)

if __name__ == '__main__':
    args = parser.parse_args()
    try:
        func = args.func
    except AttributeError:
        parser.print_help(sys.stderr)
        sys.exit(1)
    func(args)
