#!/usr/bin/env python3.8
"""Creats a Python zip archive for the input main source."""

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import shutil
import sys
import zipapp


def main():
    parser = argparse.ArgumentParser(
        'Creates a Python zip archive for the input main source')

    parser.add_argument(
        '--target_name',
        help='Name of the build target',
        required=True,
    )

    parser.add_argument(
        '--main_source',
        help='Path to the source containing the main function',
        required=True,
    )
    parser.add_argument(
        '--main_callable',
        help=
        'Name of the the main callable, that is the entry point of the generated archive',
        required=True,
    )

    parser.add_argument(
        '--gen_dir',
        help='Path to gen directory, used to stage temporary directories',
        required=True,
    )
    parser.add_argument('--output', help='Path to output', required=True)

    parser.add_argument(
        '--sources',
        help='Sources of this target, including main source',
        nargs='*',
    )
    parser.add_argument(
        '--library_infos',
        help='Path to the library infos JSON file',
        type=argparse.FileType('r'),
        required=True,
    )
    parser.add_argument(
        '--depfile',
        help='Path to the depfile to generate',
        type=argparse.FileType('w'),
        required=True,
    )

    args = parser.parse_args()

    infos = json.load(args.library_infos)
    # For writing a depfile.
    files_to_copy = []
    # For cleaning up the temporary app directory after zipapp.
    files_to_delete, dirs_to_delete = [], []

    # Temporary directory to stage the source tree for this python binary,
    # including sources of itself and all the libraries it imports.
    #
    # It is possible to have multiple python_binaries in the same directory, so
    # using target name, which should be unique in the same directory, to
    # distinguish between them.
    app_dir = os.path.join(args.gen_dir, args.target_name)
    os.makedirs(app_dir, exist_ok=True)

    # Copy over the sources of this binary.
    for source in args.sources:
        basename = os.path.basename(source)
        if basename == '__main__.py':
            print(
                '__main__.py in sources of python_binary is not supported, see https://fxbug.dev/73576',
                file=sys.stderr,
            )
            return 1
        dest = os.path.join(app_dir, basename)
        shutil.copy2(source, dest)
        files_to_delete.append(dest)

    # Make sub directories for all libraries and copy over their sources.
    for info in infos:
        dest_dir = os.path.join(app_dir, info['library_name'])
        os.makedirs(dest_dir, exist_ok=True)
        dirs_to_delete.append(dest_dir)

        for source in info['sources']:
            # NOTE: the following line is temporary to facilitate soft
            # transitioning third-party repos.
            source = os.path.join(info['source_root'], source)
            files_to_copy.append(source)
            dest = os.path.join(dest_dir, os.path.basename(source))
            shutil.copy2(source, dest)
            files_to_delete.append(dest)

    args.depfile.write('{}: {}\n'.format(args.output, ' '.join(files_to_copy)))

    # Main module is the main source without its extension.
    main_module = os.path.splitext(os.path.basename(args.main_source))[0]
    # Manually create a __main__.py file for the archive, instead of using the
    # `main` parameter from `create_archive`. This way we can import everything
    # from the main module (create_archive only `import pkg`), which is
    # necessary for including all test cases for unit tests.
    #
    # TODO(https://fxbug.dev/73576): figure out another way to support unit
    # tests when users need to provide their own custom __main__.py.
    main_file = os.path.join(app_dir, "__main__.py")
    with open(main_file, 'w') as f:
        f.write(f'from {main_module} import *\n{args.main_callable}()')
    files_to_delete.append(main_file)

    zipapp.create_archive(
        app_dir,
        target=args.output,
        interpreter='/usr/bin/env python3.8',
        compressed=True,
    )

    # Manually remove the temporary app directory and all the files, instead of
    # using shutil.rmtree. rmtree records reads on directories which throws off
    # the action tracer.
    for f in files_to_delete:
        os.remove(f)
    for d in dirs_to_delete:
        os.rmdir(d)


if __name__ == '__main__':
    sys.exit(main())
