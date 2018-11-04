#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import StringIO
import argparse
import json
import operator
import os
import sys
import tarfile
import time
import zipfile


def generate_script(images, type):
    # The bootserver must be in there or we lose.
    # TODO(mcgrathr): Multiple bootservers for different platforms
    # and switch in the script.
    [bootserver] = [image['path'] for image in images
                    if image['name'] == 'bootserver']
    script = '''\
#!/bin/sh
dir="$(dirname "$0")"
set -x
'''
    switches = dict((switch, '"$dir/%s"' % image['path'])
                    for image in images if type in image
                    for switch in image[type])
    primary = switches.get('')
    if primary:
        del switches['']
    cmd = ['exec', '"$dir/%s"' % bootserver]
    for switch, path in sorted(switches.iteritems()):
        cmd += [switch, path]
    if primary:
        cmd.append(primary)
    cmd.append('"$@"')
    script += ' '.join(cmd) + '\n'
    return script


def tgz_archiver(outfile):
    with tarfile.open(outfile, 'w:gz', dereference=True) as archive:
        while True:
            contents, name, executable = (yield)
            def sanitize_tarinfo(info):
                assert info.isfile()
                info.mode = 0o555 if executable else 0o444
                info.uid = 0
                info.gid = 0
                info.uname = ''
                info.gname = ''
                return info
            if type(contents) in (str, unicode):
                archive.add(contents, name, filter=sanitize_tarinfo)
            else:
                info = sanitize_tarinfo(tarfile.TarInfo(name))
                contents = contents()
                info.size = len(contents)
                info.mtime = time.time()
                archive.addfile(info, StringIO.StringIO(contents))


def zip_archiver(outfile):
    with zipfile.ZipFile(outfile, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.comment = 'Fuchsia build archive'
        while True:
            contents, name, executable = (yield)
            if type(contents) in (str, unicode):
                archive.write(contents, name)
            else:
                archive.writestr(name, contents())


def format_archiver(outfile, format):
    archiver = {'tgz': tgz_archiver, 'zip': zip_archiver}[format](outfile)
    archiver.next()
    return archiver


def write_archive(outfile, format, images):
    # Synthesize a sanitized form of the input.
    packed_images = []
    for image in images:
        path = image['path']
        if 'archive' in image:
            del image['archive']
        image['path'] = image['name'] + '.' + image['type']
        packed_images.append((path, image))

    # Generate scripts that use the sanitized file names.
    packed_images += [
        (lambda: generate_script([image for path, image in packed_images],
                                 'bootserver_pave'), {
            'name': 'pave',
            'type': 'sh',
            'path': 'pave.sh'
        }),
        (lambda: generate_script([image for path, image in packed_images],
                                 'bootserver_netboot'), {
            'name': 'netboot',
            'type': 'sh',
            'path': 'netboot.sh'
        })
    ]

    # Self-reference.
    packed_images.append(
        (lambda: json.dumps([image for contents, image in packed_images],
                            indent=2, sort_keys=True),
         {
             'name': 'images',
             'type': 'json',
             'path': 'images.json',
         }))

    # Canonicalize the order of the files in the archive.
    packed_images = sorted(packed_images, key=lambda packed: packed[1]['path'])

    # The archiver is a generator that we send the details about each image.
    archiver = format_archiver(outfile, format)
    for contents, image in packed_images:
        executable = image['type'] == 'sh' or image['type'].startswith('exe')
        archiver.send((contents, image['path'], executable))
    archiver.close()


def write_symbol_archive(outfile, format, ids_file, files_read):
    files_read.add(ids_file)
    with open(ids_file, 'r') as f:
        ids = [line.split() for line in f]
    archiver = format_archiver(outfile, format)
    out_ids = ''
    for id, file in ids:
        file = os.path.relpath(file)
        files_read.add(file)
        name = os.path.relpath(file, '../..')
        archiver.send((file, name, False))
        out_ids += '%s %s\n' % (id, name)
    archiver.send((lambda: out_ids, 'ids.txt', False))
    archiver.close()


def archive_format(args, outfile):
    if args.format:
        return args.format
    if outfile.endswith('.zip'):
        return 'zip'
    if outfile.endswith('.tgz') or outfile.endswith('.tar.gz'):
        return 'tgz'
    sys.stderr.write('''\
Cannot guess archive format from file name %r; use --format.
''' % outfile)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description='Pack Fuchsia build images.')
    parser.add_argument('--depfile',
                        metavar='FILE',
                        help='Write Ninja dependencies file')
    parser.add_argument('json', nargs='+',
                        metavar='FILE',
                        help='Read JSON image list from FILE')
    parser.add_argument('--pave',
                        metavar='FILE',
                        help='Write paving bootserver script to FILE')
    parser.add_argument('--netboot',
                        metavar='FILE',
                        help='Write netboot bootserver script to FILE')
    parser.add_argument('--archive',
                        metavar='FILE',
                        help='Write archive to FILE')
    parser.add_argument('--symbol-archive',
                        metavar='FILE',
                        help='Write symbol archive to FILE')
    parser.add_argument('--format', choices=['tgz', 'zip'],
                        help='Archive format (default: from FILE suffix)')
    args = parser.parse_args()

    # Keep track of every input file for the depfile.
    files_read = set()
    def read_json_file(filename):
        files_read.add(filename)
        with open(filename, 'r') as f:
            return json.load(f)

    images = reduce(operator.add,
                    (read_json_file(file) for file in args.json),
                    [])

    outfile = None

    # Write an executable script into outfile for the given bootserver mode.
    def write_script_for(outfile, mode):
        with os.fdopen(os.open(outfile, os.O_CREAT | os.O_TRUNC | os.O_WRONLY,
                               0o777),
                       'w') as script_file:
            script_file.write(generate_script(images, mode))

    # First write the local scripts that work relative to the build directory.
    if args.pave:
        outfile = args.pave
        write_script_for(args.pave, 'bootserver_pave')
    if args.netboot:
        outfile = args.netboot
        write_script_for(args.netboot, 'bootserver_netboot')

    if args.archive:
        outfile = args.archive
        archive_images = [image for image in images
                          if (image.get('archive', False) or
                              'bootserver_pave' in image or
                              'bootserver_netboot' in image)]
        files_read |= set(image['path'] for image in archive_images)
        write_archive(outfile, archive_format(args, outfile), archive_images)

    if args.symbol_archive:
        outfile = args.symbol_archive
        [ids_file] = [image['path'] for image in images
                      if image['name'] == 'build-id' and image['type'] == 'txt']
        write_symbol_archive(outfile, archive_format(args, outfile),
                             ids_file, files_read)

    if outfile and args.depfile:
        with open(args.depfile, 'w') as depfile:
            depfile.write('%s: %s\n' % (outfile, ' '.join(sorted(files_read))))


if __name__ == "__main__":
    main()
