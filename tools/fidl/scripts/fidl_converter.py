#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys
from pathlib import Path
from multiprocessing.dummy import Pool

FUCHSIA_DIR = Path(os.environ['FUCHSIA_DIR'])


def main(args):
    """Converts as many FIDL files as possible by invoking `fx build`, then
    running through a number of strategies to match the converted file output.
    This script assumes that the fidlc has been instrumented such that it
    produces a `.fidl.new` type output for each input `.fidl` file.
    """

    if args.dryrun:
        print('\nDRY RUN!')
    else:
        print('\nREAL RUN!')

    if args.rebuild:
        # Note: This includes all of the "kitchen_sink" and "buildbot:core", but
        # unfortunately there are still some build targets that are not included
        # in these catch-all targets.  Those are manually listed at th marked
        # point, but will likely change in the future (ie, the targets could be
        # added to the catch-alls, or removed completely.  Additionally, other
        # targets that are missed by the catch-alls could be added as well).
        print('\nSETTING ENVIRONMENT...')
        subprocess.check_call(
            [
                'fx', '--dir=out/default', 'set', 'workstation.x64',
                '--with=//bundles/fidl:tests', '--with=//bundles:kitchen_sink',
                '--with=//bundles:tests', '--with=//bundles/buildbot:core',
                '--with=//sdk/fidl/fuchsia.firebase.messaging:fuchsia.firebase.messaging',
                '--with=//sdk/fidl/fuchsia.metricbroker:fuchsia.metricbroker',
                '--with=//sdk/fidl/fuchsia.process:fuchsia.process',
                '--with=//sdk/fidl/fuchsia.process.init:fuchsia.process.init',
                '--with=//sdk/fidl/fuchsia.terminal:fuchsia.terminal',
                '--with=//src/camera/bin/factory:fuchsia.factory.camera',
                '--with=//src/media/vnext/fidl/fuchsia.audio:fuchsia.audio',
                '--with=//src/media/vnext/fidl/fuchsia.media2:fuchsia.media2',
                '--with=//src/media/vnext/fidl/fuchsia.mediastreams:fuchsia.mediastreams',
                '--with=//src/media/vnext/fidl/fuchsia.mem2:fuchsia.mem2',
                '--cargo-toml-gen'
            ],
            stdout=sys.stdout)

        print('\nBUILDING...')
        subprocess.check_call(['fx', 'build'], stdout=sys.stdout)

    print('\nMATCHING... (takes a few min)')
    converted, unconverted = match_converted_files()
    ready = len(converted)
    total = ready + len(unconverted)

    print('\nCOULD NOT MATCH THE FOLLOWING FILES:')
    for u in unconverted:
        print(u)

    if args.dryrun:
        print('MATCHED %s OF %d FIDL FILES' % (ready, total))
    else:
        for old, new in converted.items():
            try:
                subprocess.check_call(['cp', '-fT', new, old])
            except subprocess.CalledProcessError as e:
                print(e.output)
        print('SUCCESSFULLY CONVERTED %s OF %d FIDL FILES' % (ready, total))


def match_converted_files():
    """Attempts to match each source FIDL file with its converted `.fidl.new`
    counterpart in the `out/default` directory.  Returns a tuple, with the first
    value being a map of old_file -> converted_file, and the second being a list
    of old files that could not be converted.
    """

    converted = {}
    unconverted = []

    # Match each source file with its converted copy.
    for path in FUCHSIA_DIR.rglob('*.fidl'):
        if not path.is_file():
            continue
        old_syntax_path = path.relative_to(FUCHSIA_DIR)

        # These are not fuchsia.git source files - ignore them.
        if str(old_syntax_path).startswith('out/'):
            continue
        if str(old_syntax_path).startswith('prebuilt/'):
            continue
        if str(old_syntax_path).startswith('third_party/'):
            continue
        if str(old_syntax_path).startswith('vendor/'):
            continue

        new_syntax_path = FUCHSIA_DIR / 'out/default/fidling/gen' / old_syntax_path
        new_syntax_path = new_syntax_path.with_suffix('.fidl.new')

        # Special handling for these.
        if str(old_syntax_path).startswith('zircon/tools/kazoo'):
            converted[str(old_syntax_path)] = str(new_syntax_path) \
                .replace('/fidling/gen/', '/host_x64/gen/')
            continue
        if str(old_syntax_path).startswith('zircon/vdso'):
            converted[str(old_syntax_path)] = str(new_syntax_path) \
                .replace('/fidling/gen/zircon/vdso', '/gen/zircon')
            continue

        # The default case: the file's output location matches its location in
        # the source, save a slight redirection to the output directory.
        if new_syntax_path.exists():
            converted[str(old_syntax_path)] = str(new_syntax_path)
            continue

        # Sometimes, a file stored in `foo/fidl/my.fidl` is output into
        # a directory called `foo` rather than `foo/fidl`.  Check for such
        # cases.
        new_syntax_path = '/'.join(str(new_syntax_path).rsplit('/fidl/', 1))
        if Path(new_syntax_path).exists():
            converted[str(old_syntax_path)] = str(new_syntax_path)
            continue

        unconverted.append(str(old_syntax_path))

    # But wait!  There's still a couple of things we can try for all of the
    # old syntax FIDL files we've not been able to successfully pair with a
    # converted copy.  First, we'll try using the `fx gn outputs` command on
    # each of the old files, in order to find outputs written to unorthodox
    # output directory paths.  Because this command can take a long time, we'll
    # use a thread pool to parallelize the invocations.
    pool = Pool(16)
    for matched in pool.map(match_converted_file, unconverted):
        if matched is not None:
            converted[matched[0]] = matched[1]
            unconverted.remove(matched[0])

    # One more thing we can try: just attempt to run fidlc on each of the
    # remaining files.  This will fail for FIDL libraries that import
    # dependencies via the `using` command, or are defined over multiple source
    # files, but it will at least cover us for simple test files (DIFL, etc).
    for matched in pool.map(try_convert_fidl_file, unconverted):
        if matched is not None:
            converted[matched[0]] = matched[1]
            unconverted.remove(matched[0])

    # Whatever is left at this point truly, really needs to be converted by
    # hand.
    return converted, unconverted


def match_converted_file(old_syntax_path_str):
    """This function uses `fx gn outputs` to search for any output files located
    in unconventionally named outputs directories. It will return None on
    failure, and a (old_file_path, converted_file_path) tuple on success.
    """

    new_syntax_path = Path(old_syntax_path_str).with_suffix('.fidl.new')
    new_file_name = str(new_syntax_path.name)
    result = subprocess.run(
        ['fx', 'gn', 'outputs', 'out/default', old_syntax_path_str],
        capture_output=True)

    for line in result.stdout.decode('utf-8').splitlines():
        stripped = str(line.strip())
        if stripped.endswith(new_file_name):
            # print(' * ' + old_syntax_path_str + ' [MATCHED]')
            return old_syntax_path_str, "out/default/" + stripped

    # print(' * ' + old_syntax_path_str + ' [NOT FOUND]')
    return None


def try_convert_fidl_file(old_syntax_path_str):
    """This function will successfully convert any FIDL library that does not
    have any `using` imports and is defined in a single source file. It will
    return None on failure, and a (old_file_path, converted_file_path) tuple on
    success.
    """

    tmpdir = Path.home() / 'tmp/fidl-migration'
    new_syntax_path = tmpdir / old_syntax_path_str
    new_syntax_path = new_syntax_path.with_suffix('.fidl.new')
    new_syntax_dir = new_syntax_path.parent
    subprocess.check_call(['mkdir', '-p', new_syntax_dir], stdout=sys.stdout)

    # Try to convert the file assuming that it is a standalone library with no
    # imports.  If it works, great.  If not, return None, so that we may record
    # this file as unconvertable.
    fidlc = FUCHSIA_DIR / 'out/default/host_x64/exe.unstripped/fidlc'
    old_file = FUCHSIA_DIR / Path(old_syntax_path_str)
    try:
        subprocess.check_call(
            [
                fidlc, '--experimental', 'old_syntax_only', '--convert-syntax',
                new_syntax_dir, '--files', old_file
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError as e:
        # print(' * ' + old_syntax_path_str + ' [ERRORED]')
        return None

    # print(' * ' + old_syntax_path_str + ' [MATCHED]')
    return old_syntax_path_str, str(new_syntax_path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description=
        'Copies files converted by fidlc via a flag-enabled mode back into the source tree.'
    )
    parser.add_argument('--dry-run', dest='dryrun', action='store_true')
    parser.add_argument('--no-dry-run', dest='dryrun', action='store_false')
    parser.set_defaults(dryrun=True)
    parser.add_argument('--rebuild', dest='rebuild', action='store_true')
    parser.add_argument('--no-rebuild', dest='rebuild', action='store_false')
    parser.set_defaults(rebuild=True)
    main(parser.parse_args())
