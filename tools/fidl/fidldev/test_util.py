import argparse
import contextlib
import pprint

import env

# mock out these environment consts before importing anything else
MOCK_FUCHSIA_DIR = 'fuchsia_dir'
MOCK_BUILD_DIR = 'out/default'
env.FUCHSIA_DIR = MOCK_FUCHSIA_DIR
env.BUILD_DIR = MOCK_BUILD_DIR
env.PLATFORM = 'linux'
env.MODE = 'clang'

import fidldev
import util


@contextlib.contextmanager
def mocked_func(func_name, mocked_func):
    """ Patch util.[func_name] with mocked_func within the specified context. """
    original = getattr(util, func_name)
    try:
        setattr(util, func_name, mocked_func)
        yield
    finally:
        setattr(util, func_name, original)


def create_fixed_func(return_values):
    """ Returns a function that successively returns each of the provided |return_values|. """
    return_values = iter(return_values)

    def mocked(*args, **kwargs):
        return next(return_values)

    return mocked


def get_commands(mocks, test_cmd):
    """ Run |test_cmd| with the provided |mocks|, and return the commands that fidldev would have run. """
    mocked_funcs = [
        mocked_func(name, create_fixed_func(values))
        for name, values in mocks.items()
    ]

    commands = []

    def mocked_run(command, dry_run):
        commands.append(command)

    mocked_funcs.append(mocked_func('run', mocked_run))

    with contextlib.ExitStack() as stack:
        for func in mocked_funcs:
            stack.enter_context(func)
        args = fidldev.parser.parse_args(test_cmd)
        args.func(args)

    return commands
