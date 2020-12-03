#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
from pathlib import Path
import pprint
import os
from typing import Dict, List
import sys

import errors
from gen import generate_test
import scaffolding
from transitions import transitions, Binding, Transition, Type, FIDL_ASSISTED
from serialize import read_transitions, write_transitions, to_flags
from util import print_err, white, print_warning

EXAMPLES = """
This tool will create the source compatibility test structure and walk you
through the implementation step by step, as if you were implementing the
transition.

Examples:

Generate a test by specifying the types of transitions for each binding with the
gen command:

    main.py gen protocol-method-add \\
        --rust mixed \\
        --go fidl-assisted

The test will only include the specified bindings. When you are ready to add
more bindings, rerun the tool with any new bindings:

    main.py gen protocol-method-add \\
        --rust mixed \\
        --go fidl-assisted \\
        --hlcpp fidl-assisted

You can get the flags that were invoked for a particular test by running

    main.py describe protocol-method-add

If you ever exit the tool while working an test, you can pick up where you left
off without respecifying all of the flags, by using --continue:

    main.py gen protocol-method-add --continue

"""


def gen(args):
    test_dir = Path(os.path.join(args.root, args.name))
    os.makedirs(test_dir, exist_ok=True)

    if args.continue_:
        transitions_by_binding = read_transitions(test_dir)
    else:
        transitions_by_binding = transitions_from_args(args)
        try:
            existing = read_transitions(test_dir)
        except FileNotFoundError:
            write_transitions(test_dir, transitions_by_binding)
        else:
            if transitions_by_binding != existing:
                print_warning(
                    f'specified transitions do not match previously existing ones for {args.name}:'
                )
                print('your parameters:')
                print(f'  {to_flags(transitions_by_binding)}')
                print('existing parameters:')
                print(f'  {to_flags(existing)}')
                if input('\ncontinue? (Y/n) ') == 'n':
                    return
                write_transitions(test_dir, transitions_by_binding)

    generate_test(args.fidl, test_dir, transitions_by_binding)


def describe(args):
    root = Path(os.path.join(args.root, args.name))
    transitions_by_binding = read_transitions(root)
    print(white(f'Parameters for test {args.name}:'))
    print(to_flags(transitions_by_binding))


def transitions_from_args(args) -> Dict[Binding, Transition]:
    transitions_by_binding = {}
    for binding in Binding:
        transition = getattr(args, binding.value)
        if transition is None:
            continue
        if transition not in transitions:
            print_err(
                f'error: undefined transition {transition} for binding {binding.value}'
            )
            return
        transitions_by_binding[binding] = transitions[transition]
    return transitions_by_binding


parser = argparse.ArgumentParser(
    description="Generate FIDL source compatibility test scaffolding",
    formatter_class=argparse.RawDescriptionHelpFormatter,
    epilog=EXAMPLES)
parser.add_argument(
    '--root',
    help=
    'Directory that all other paths should be relative to. Useful for testing. Defaults to %(default)s)',
    default=os.path.join(
        os.environ['FUCHSIA_DIR'], 'src/tests/fidl/source_compatibility'),
)
subparsers = parser.add_subparsers()

gen_parser = subparsers.add_parser("gen", help="Generate or modify a test")
gen_parser.set_defaults(func=gen)
gen_parser.add_argument(
    'fidl',
    help=
    'The FIDL library name. Must be a valid library name component, e.g. addmethod'
)
gen_parser.add_argument(
    'name',
    help=
    'The test name. Ideally, this should match [parent]-[target]-[change] format specificied in the ABI/API compatibility guide, e.g. protocol-method-add'
)
gen_parser.add_argument(
    '--continue',
    dest='continue_',
    help='Resume running the tool from an existing test.',
    action='store_true')
valid_transitions = set(transitions)
for binding in Binding:
    name = binding.value
    gen_parser.add_argument(
        f'--{name}',
        help=
        f'The type of transition for {name}. Refer to source_compatibility/README.md for a description of the transitions',
        choices=valid_transitions
    )

describe_parser = subparsers.add_parser(
    'describe',
    help='Get the command line arguments used to generate an existing test')
describe_parser.set_defaults(func=describe)
describe_parser.add_argument('name', help='the name of the test/directory')

if __name__ == '__main__':
    args = parser.parse_args()
    try:
        func = args.func
    except AttributeError:
        parser.print_help(sys.stderr)
        sys.exit(1)
    func(args)
