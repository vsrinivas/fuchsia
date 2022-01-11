#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import io
import argparse
import functools
import json
import operator
import os
import sys
import tarfile
import time
import zipfile
from functools import reduce


def generate_script(
        binary_name, images, board_name, type, additional_arguments):
    # The binary must be in there or we lose.
    # TODO(mcgrathr): Multiple bootservers for different platforms
    # and switch in the script.
    [binary
    ] = [image['path'] for image in images if image['name'] == binary_name]
    script = '''\
#!/bin/sh
dir="$(dirname "$0")"
set -x
'''
    switches = dict(
        (switch, '"$dir/%s"' % image['path']) for image in images
        if type in image for switch in image[type])
    cmd = ['exec', '"$dir/%s"' % binary]
    if binary_name == "bootserver":
        if board_name:
            cmd += ['--board_name', '"%s"' % board_name]

    if additional_arguments:
        cmd += [additional_arguments]

    for switch, path in sorted(switches.items()):
        cmd += [switch, path]
    cmd.append('"$@"')
    script += ' '.join(cmd) + '\n'
    return script


class TarArchiver(object):
    """Public interface needs to match {Nil,Zip}Archiver."""

    def __init__(self, outfile, compress=True):
        mode = 'w'
        # If compression is requested, use the mode 'w:gz', which adds gzip
        # compression to the output file.
        if compress:
            mode += ':gz'

        self._archive = tarfile.open(outfile, mode, dereference=True)

    def __enter__(self):
        return self

    def __exit__(self, unused_type, unused_value, unused_traceback):
        self._archive.close()

    @staticmethod
    def _sanitize_tarinfo(executable, info):
        assert info.isfile()
        info.mode = 0o555 if executable else 0o444
        info.uid = 0
        info.gid = 0
        info.uname = ''
        info.gname = ''
        return info

    def add_path(self, path, name, executable):
        self._archive.add(
            path,
            name,
            filter=functools.partial(self._sanitize_tarinfo, executable))

    def add_contents(self, contents, name, executable):
        info = self._sanitize_tarinfo(executable, tarfile.TarInfo(name))
        info.size = len(contents)
        info.mtime = time.time()
        self._archive.addfile(info, io.BytesIO(contents))


# Produces a deflated zip archive.
class ZipArchiver(object):
    """Public interface needs to match TarArchiver."""

    def __init__(self, outfile):
        self._archive = zipfile.ZipFile(outfile, 'w', zipfile.ZIP_DEFLATED)
        self._archive.comment = 'Fuchsia build archive'.encode()

    def __enter__(self):
        return self

    def __exit__(self, unused_type, unused_value, unused_traceback):
        self._archive.close()

    def add_path(self, path, name, unused_executable):
        self._archive.write(path, name)

    def add_contents(self, contents, name, unused_executable):
        self._archive.writestr(name, contents)


def format_archiver(outfile):
    if outfile.endswith('.tar'):
        return TarArchiver(outfile, compress=False)
    if outfile.endswith('.tgz') or outfile.endswith('.tar.gz'):
        return TarArchiver(outfile, compress=True)
    if outfile.endswith('.zip'):
        return ZipArchiver(outfile)
    sys.stderr.write(
        '''\
Cannot guess archive format from file name %r; use --format.
''' % outfile)
    sys.exit(1)


def write_archive(outfile, images, board_name, additional_bootserver_arguments):
    # Synthesize a sanitized form of the input.
    path_images = []
    for image in images:
        path = image['path']
        if 'archive' in image:
            del image['archive']
        image['path'] = image['name'] + '.' + image['type']
        path_images.append((path, image))

    # Generate scripts that use the sanitized file names.
    content_images = [
        (
            generate_script(
                "bootserver", [image for path, image in path_images],
                board_name, 'bootserver_pave',
                additional_bootserver_arguments), {
                    'name': 'pave',
                    'type': 'sh',
                    'path': 'pave.sh'
                }),
        (
            generate_script(
                "bootserver", [image for path, image in path_images],
                board_name, 'bootserver_pave_zedboot',
                additional_bootserver_arguments +
                " --allow-zedboot-version-mismatch"), {
                    'name': 'pave-zedboot',
                    'type': 'sh',
                    'path': 'pave-zedboot.sh'
                }),
        (
            generate_script(
                "bootserver", [image for path, image in path_images],
                board_name, 'bootserver_netboot',
                additional_bootserver_arguments), {
                    'name': 'netboot',
                    'type': 'sh',
                    'path': 'netboot.sh'
                })
    ]

    # Self-reference.
    content_images.append(
        (
            json.dumps(
                [image for _, image in (path_images + content_images)],
                indent=2,
                sort_keys=True), {
                    'name': 'images',
                    'type': 'json',
                    'path': 'images.json',
                }))

    # Canonicalize the order of the files in the archive.
    path_images = sorted(path_images, key=lambda pair: pair[1]['path'])
    content_images = sorted(content_images, key=lambda pair: pair[1]['path'])

    def is_executable(image):
        return image['type'] == 'sh' or image['type'].startswith('exe')

    with format_archiver(outfile) as archiver:
        for path, image in path_images:
            archiver.add_path(path, image['path'], is_executable(image))
        for contents, image in content_images:
            archiver.add_contents(
                contents.encode(), image['path'], is_executable(image))


def main():
    parser = argparse.ArgumentParser(description='Pack Fuchsia build images.')
    parser.add_argument(
        '--depfile', metavar='FILE', help='Write Ninja dependencies file')
    parser.add_argument(
        'json',
        nargs='+',
        metavar='FILE',
        help='Read JSON image list from FILE')
    parser.add_argument(
        '--pave', metavar='FILE', help='Write paving bootserver script to FILE')
    parser.add_argument(
        '--pave_zedboot',
        metavar='FILE',
        help='Write zedboot paving bootserver script to FILE')
    parser.add_argument(
        '--netboot',
        metavar='FILE',
        help='Write netboot bootserver script to FILE')
    parser.add_argument(
        '--fastboot_boot',
        metavar='FILE',
        help="Write fastboot boot script to FILE")
    parser.add_argument(
        '--archive', metavar='FILE', help='Write archive to FILE')
    parser.add_argument(
        '--format',
        choices=['tar', 'tgz'],
        help='Archive format (default: from FILE suffix)')
    parser.add_argument('--board_name', help='Board name images were built for')
    parser.add_argument(
        '--additional_bootserver_arguments',
        action='append',
        default=[],
        help='additional arguments to pass to bootserver in generated scripts')
    args = parser.parse_args()

    # Keep track of every input file for the depfile.
    files_read = set()

    def read_json_file(filename):
        files_read.add(filename)
        with open(filename, 'r') as f:
            return json.load(f)

    images = reduce(
        operator.add, (read_json_file(file) for file in args.json), [])

    outfile = None

    # Write an executable script into outfile for the given bootserver mode.
    def write_script_for(outfile, mode, binary_name="bootserver"):
        with os.fdopen(os.open(outfile, os.O_CREAT | os.O_TRUNC | os.O_WRONLY,
                               0o777), 'w') as script_file:
            additional_args = ''
            if mode != 'fastboot_boot':
                additional_args = ''.join(args.additional_bootserver_arguments)
            script_file.write(
                generate_script(
                    binary_name, images, args.board_name, mode, additional_args))

    # First write the local scripts that work relative to the build directory.
    if args.pave:
        outfile = args.pave
        write_script_for(args.pave, 'bootserver_pave')
    if args.pave_zedboot:
        outfile = args.pave_zedboot
        write_script_for(args.pave_zedboot, 'bootserver_pave_zedboot')
    if args.netboot:
        outfile = args.netboot
        write_script_for(args.netboot, 'bootserver_netboot')
    if args.fastboot_boot:
        outfile = args.fastboot_boot
        write_script_for(
            args.fastboot_boot, "fastboot_boot", binary_name="fastboot")

    if args.archive:
        outfile = args.archive
        archive_images = [
            image for image in images if image.get('archive', False)
        ]
        files_read |= set(image['path'] for image in archive_images)
        write_archive(
            outfile, archive_images, args.board_name,
            ' '.join(args.additional_bootserver_arguments))

    if outfile and args.depfile:
        with open(args.depfile, 'w') as depfile:
            depfile.write('%s: %s\n' % (outfile, ' '.join(sorted(files_read))))


if __name__ == "__main__":
    main()
