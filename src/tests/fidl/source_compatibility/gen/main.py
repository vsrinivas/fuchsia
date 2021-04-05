#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
from pathlib import Path
import os
import signal
import shutil
import sys

import generate_test
import generate_docs
from reverse_test import reverse_test
from util import print_err, TEST_FILE, white, find_tests, print_warning
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


def gen_reverse(args):
    src_dir = Path(os.path.join(args.root, args.source))
    with open(src_dir / TEST_FILE, 'r') as f:
        src_test = CompatTest.fromdict(json.load(f))

    new_test, old_to_new_files = reverse_test(src_test)

    target_dir = Path(os.path.join(args.root, args.target))
    os.makedirs(target_dir, exist_ok=True)
    for old, new in old_to_new_files.items():
        old = src_dir / old
        new = target_dir / new
        os.makedirs(new.parent, exist_ok=True)
        shutil.copyfile(old, new)
    new_test.save(target_dir / TEST_FILE)
    generate_test.regen_files(target_dir, new_test)


def regen(args):
    tests = args.tests or find_tests(args.root)
    for name in tests:
        print(f'Regenerating files for {name}')
        test_dir = Path(os.path.join(args.root, name))
        with open(test_dir / TEST_FILE, 'r') as f:
            test = CompatTest.fromdict(json.load(f))
        generate_test.regen_files(test_dir, test)
    if not tests:
        print_warning('No tests found')
    else:
        print(
            white('Done! Run fx-format to get rid of formatting differences.'))


def regen_toc(args):
    all_tests = []
    for test_root in find_tests(args.root):
        with open(test_root / TEST_FILE, 'r') as f:
            test = CompatTest.fromdict(json.load(f))
        all_tests.append((test_root, test))
    if not all_tests:
        print_warning('No tests found')
    generate_docs.regen_toc(all_tests)


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

gen_reverse_parser = subparsers.add_parser(
    "generate_reverse",
    help=
    "Generate a source compatibility test that runs an existing one in reverse. See tool README for details."
)
gen_reverse_parser.set_defaults(func=gen_reverse)
gen_reverse_parser.add_argument('source', help='Name of the test to reverse.')
gen_reverse_parser.add_argument(
    'target', help='Name of the (new) reversed test.')

regen_toc_parser = subparsers.add_parser(
    'regen_toc',
    help=
    'Regenerate the docs compatibility guide TOC, e.g. when you add/remove a test case.'
)
regen_toc_parser.set_defaults(func=regen_toc)

if __name__ == '__main__':
    args = parser.parse_args()
    try:
        func = args.func
    except AttributeError:
        parser.print_help(sys.stderr)
        sys.exit(1)
    func(args)
