#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime
import json
import logging
import os
import re
import subprocess
import sys
import tempfile

tool_description = """Move directory to new location in source tree


This is a tool to move a directory of source code from one place in the
Fuchsia Platform Source Tree to another location in a minimally invasive
manner.  This script attempts to identify references to the original location
and updates them or generates artifacts that transparently forward to the
new location.  To use:

1.) Check out origin/master and configure a build in the default build
directory with 'fx set'
2.) Run 'scripts/move/source/move_source.py <source> <dest>'. This will
generate a new git branch and create a commit with a description of what the
tool did.
3.) Examine the references that the tool reports that it could not handle
and fix up as needed with 'git commit -a'
4.) Upload for review

"""

fuchsia_root = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))


"""
Creates a git branch for a move and checks it out.
"""


def create_branch_for_move(source, dest, dry_run):
    branch_name = 'move_%s_to_%s' % (source.replace('/', '_'),
                                     dest.replace('/', '_'))

    run_command(['git', 'checkout', '-b', branch_name, '--track',
                 'origin/master'], dry_run)

"""
Guess if a target is a go library target, which requires a specialized
forwarding target type.
"""


def is_go_library_target(target):
    return (target['type'] == 'action' and
            target['script'] == '//build/go/gen_library_metadata.py')


"""
Stores information about a forwarding GN target to generate.
"""


class ForwardingTarget:

    def __init__(self, label, target):
        self.label = label
        self.testonly = target['testonly']
        self.is_go_library = is_go_library_target(target)

    def __repr__(self):
        return 'ForwardingTarget(%s, %s, %s)' % (self.label, self.testonly,
                                                 self.is_go_library)

    def __str__(self):
        return '%s testonly %s go_library %s' % (
               self.label, self.testonly, self.is_go_library)


"""
Finds all external references to GN targets in the source directory and
records information needed to generate forwarding targets.
"""


def find_referenced_targets(build_graph, source):
    # List of targets in the source directory tree. Each entry is a tuple
    # of (string, boolean)
    targets_in_source = []

    # Set of targets (identified by gn label) in the source directory that
    # are referenced by targets outside the source directory.
    referenced_targets = set()

    for label, target in build_graph.items():
        if label.startswith('//' + source):
            targets_in_source.append(ForwardingTarget(label, target))
        else:
            all_deps = target['deps']
            if 'public_deps' in target:
                all_deps = all_deps + target['public_deps']
            for dep in all_deps:
                # Remove the toolchain qualifier, if present
                if '(' in dep:
                    dep = dep[0:dep.find('(')]
                if dep.startswith('//' + source):
                    referenced_targets.add(dep)

    logging.debug('targets_in_source %s' % targets_in_source)
    logging.debug('referenced_targets %s' % referenced_targets)

    forwarding_targets = []
    # compute forwarding targets to create
    for target in sorted(targets_in_source):
        if target.label in referenced_targets:
            logging.debug(
                'Need to generate forwarding target for %s' % target)
            forwarding_targets.append(target)

    return forwarding_targets


"""
Finds all references to the source path that we don't know how to automatically
handle so the user can examine and update as needed.
"""


def find_unknown_references(source):
    unknown_references = []
    jiri_grep_args = ['jiri', 'grep', source]
    logging.debug('Running %s' % jiri_grep_args)
    grep_results = subprocess.check_output(jiri_grep_args, cwd=fuchsia_root)
    for line in grep_results.splitlines():
        file, match = line.split(':', 1)
        if not os.path.normpath(file).startswith(source):
            if '#include' in match:
                continue
            if file.endswith('BUILD.gn'):
                continue
            unknown_references.append(line)
    return unknown_references


"""
Partial information about the contents of a build file. Can be combined with
other instances and written out to a file object.
"""


class PartialBuildFile():

    def __init__(self):
        self.imports = set()
        self.snippet = ''

    def merge(self, other):
        self.imports |= other.imports
        self.snippet += '\n' + other.snippet

    def write(self, f):
        for import_path in sorted(self.imports):
            f.write('''
import("%s")''' % import_path)
        f.write(self.snippet)


"""
Generates a partial build file representing a forwarding target.
Returns:

    - path to BUILD.gn file
    - partial GN build file contents
"""


def generate_forwarding_target(target, source, dest):
    label_path, target_name = target.label.split(':')
    containing_directory = label_path[2:]
    build_file_path = os.path.join(
        fuchsia_root, containing_directory, 'BUILD.gn')
    imports = set()
    # compute label relative to directory. This will be the same in the
    # source and destination
    relative_label = os.path.relpath(containing_directory, source)

    dest_path = os.path.normpath(os.path.join(dest, relative_label))
    dest_label = '//%s:%s' % (dest_path, target_name)

    logging.debug('relative_label %s dest_label %s target_name %s' % (
        relative_label, dest_label, target_name))

    build = PartialBuildFile()

    build.snippet = '''
# Do not use this target directly, instead depend on %s.''' % dest_label

    if target.is_go_library:
        build.imports.add('//build/go/go_library.gni')
        build.snippet += '''
go_library("%s") {
  name = "%s_forwarding_target"

  deps = [
''' % (target_name, target_name)

    else:
        build.snippet += '''
group("%s") {
  public_deps = [
''' % target_name

    build.snippet += '''    "%s"
  ]
''' % dest_label

    if target.testonly:
        build.snippet += '''  testonly = true
'''

    build.snippet += '''}
'''

    return build_file_path, build


"""
Write out forwarding build rules for a set of forwarding targets.
"""


def write_forwarding_build_rules(forwarding_targets, source, dest, dry_run):
    build_files = {}
    for target in forwarding_targets:
        path, build = generate_forwarding_target(target, source, dest)
        if path not in build_files:
            build_files[path] = PartialBuildFile()

        build_files[path].merge(build)

    for path, build in build_files.iteritems():
        if not dry_run:
            if not os.path.exists(path):
                dest_dir = os.path.dirname(path)
                if not os.path.exists(dest_dir):
                    os.makedirs(dest_dir)
                with open(path, 'w') as f:
                    write_copyright_header(f, '#')
            with open(path, 'a') as f:
                build.write(f)
            run_command(['git', 'add', path], dry_run)


"""
Appends contents to a BUILD.gn file in a given directory. Creates the file
if necessary, including copyright headers. Does not format the file it
creates or modifies.
"""


def append_to_gn_file(directory, contents):
    dest_path = os.path.join(fuchsia_root, directory, 'BUILD.gn')
    if not os.path.exists(dest_path):
        dest_dir = os.path.dirname(dest_path)
        if not os.path.exists(dest_dir):
            os.makedirs(dest_dir)
        with open(dest_path, 'w') as f:
            write_copyright_header(f, '#')
    with open(dest_path, 'a') as f:
        f.write(contents)
    dry_run = False
    run_command(['git', 'add', dest_path], False)


"""
Writes a copyright header for the current year into a provided open file.
"""


def write_copyright_header(f, comment):
    copyright_header = '''%s Copyright %s The Fuchsia Authors. All rights reserved.
%s Use of this source code is governed by a BSD-style license that can be
%s found in the LICENSE file.

''' % (comment, datetime.date.today().year, comment,
        comment)
    f.write(copyright_header)


"""
Generates a forwarding header.
"""


def generate_forwarding_header(header, source, dest, dry_run):
    header_dir = os.path.dirname(header)
    relative_path = os.path.normpath(os.path.relpath(header, source))
    dest_path = os.path.join(dest, relative_path)

    logging.debug('Generating forwarding header %s pointing to %s' % (
        header, dest_path))

    if dry_run:
        return

    if not os.path.exists(header_dir):
        os.makedirs(header_dir)
    with open(header, 'w') as f:
        write_copyright_header(f, '//')
        # Formatter will generate a proper header guard
        f.write('#pragma once\n')

        f.write('''
// Do not use this header directly, instead use %s.

''' % dest_path)
        f.write('#include "%s"\n' % dest_path)

    run_command(['git', 'add', header], dry_run)


"""
Generates a commit message for the move including general information
about the move as well as a list of generated forwarding artifacts.
"""


def generate_commit_message(source, dest, forwarding_targets,
                            forwarding_headers, change_id):
    commit_message = '''[%s] Move %s to %s

This moves the contents of the directory:
  %s
to the directory:
  %s

to better match Fuchsia's desired source layout:
https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/source_code/layout.md

''' % (os.path.basename(dest), source, dest, source, dest)

    if len(forwarding_targets):
        commit_message += '''
This commit includes the following forwarding build targets to ease the
transition to the new layout:

'''
        for target in sorted(forwarding_targets):
            commit_message += '  %s\n' % target.label
        commit_message += '''
New code should rely on the new paths for these targets. A follow up commit
will remove the forwarding targets once all references are updated.
'''

    if len(forwarding_headers):
        commit_message += '''
This commit includes the following forwarding headers to ease the
transition to the new layout:

'''
        for header in sorted(forwarding_headers):
            commit_message += '  %s\n' % header
        commit_message += '''

New code should rely on the new paths for these headers. A follow up commit
will remove the forwarding headers once all references are updated.
'''

    commit_message += '''
This commit is generated by the script //scripts/move_source/move_source.py and
should contain no behavioral changes.

Bug: 36063
'''

    if change_id:
        commit_message += '''
Change-Id: %s
''' % change_id
    return commit_message


"""
This finds all header files in a source directory that are referenced by
files outside the source directory.
"""


def find_externally_referenced_headers(source):
    externally_referenced_headers = set()
    jiri_grep_args = ['jiri', 'grep', '#include .%s' % source]
    logging.debug('Running %s' % jiri_grep_args)

    # search for all includes
    grep_results = subprocess.check_output(jiri_grep_args, cwd=fuchsia_root)
    for line in grep_results.splitlines():
        print line
        file, include = line.split(':', 1)
        # filter out includes from within the source directory
        if not os.path.normpath(file).startswith(source):
            include = include[len('#include "'):-1]
            externally_referenced_headers.add(include)

    logging.debug('Externally referenced headers: %s' %
                  externally_referenced_headers)
    return externally_referenced_headers


"""
This runs 'gn desc' and parses the output to produce a representation of the
build graph.
"""


def extract_build_graph(label_or_pattern='//*'):
    out_dir = subprocess.check_output(['fx', 'get-build-dir']).strip()

    args = ['fx', 'gn', 'desc', out_dir, label_or_pattern, '--format=json',
            '--all-toolchains']
    json_build_graph = subprocess.check_output(args)
    return json.loads(json_build_graph)


"""
Moves everything from the directory 'source' to 'dest'
"""


def move_directory(source, dest, dry_run, repository=fuchsia_root):
    # git mv requires the parent of the destination directory to exist in
    # order to move a directory.
    dest_parent = os.path.dirname(dest)
    dest_parent_abs = os.path.join(repository, dest_parent)
    if not os.path.exists(dest_parent_abs):
        os.makedirs(dest_parent_abs)
    run_command(['git', 'mv', source, dest], dry_run, cwd=repository)

"""
Updates all references to 'source' in files in the directory 'dest'.
"""


def update_all_references(source, dest, dry_run):
    for dirpath, dirnames, filenames in os.walk(dest):
        for name in filenames:
            filepath = os.path.join(dirpath, name)
            logging.debug('converting %s to %s in %s' % (source, dest, filepath))

            # On a dry run, verify that the input_file can be read
            # On a normal run, read the input, write with new references
            # to a temp file, and then swap the temp file over the input file
            with open(filepath, 'r') as input_file:
                lines = input_file.readlines()
            if not dry_run:
                filepath_temp = filepath + ".temp"
                with open(filepath_temp, 'w') as output_file:
                    for line in lines:
                        output_file.write(re.sub(source, dest, line))
                os.rename(filepath_temp, filepath)


"""
Make a git commit with the provided commit message.
"""


def commit_with_message(commit_message, dry_run, no_commit):
    with tempfile.NamedTemporaryFile(delete=False) as commit_message_file:
        commit_message_file.write(commit_message)
    if not no_commit:
        run_command(['git', 'commit', '-a', '--file=%s' %
                     commit_message_file.name], dry_run)
    os.remove(commit_message_file.name)


"""
Logs a command and then runs it in the Fuchsia root if dry_run is false.
"""


def run_command(command, dry_run, cwd=fuchsia_root):
    logging.debug('Running %s in cwd %s' % (command, cwd))
    if not dry_run:
        subprocess.check_call(command, cwd=cwd)


def main():
    parser = argparse.ArgumentParser(
        description=tool_description,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument('source', help='Source path')
    parser.add_argument('dest', help='Destination path')
    parser.add_argument('--no-branch', action='store_true',
                        help='Do not create a git branch for this move')
    parser.add_argument('--no-commit', action='store_true',
                        help='Do not create a git commit for this move')
    parser.add_argument('--change-id',
                        help='Change-Id value to use in generated commit. ' +
                        'Use this to generate new commits associated with ' +
                        'an existing review')
    parser.add_argument('--dry-run', '-n', action='store_true',
                        help='Dry run - log commands, but do not modify tree')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Enable verbose debug logging')

    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    source = os.path.normpath(args.source)
    source_abs = os.path.join(fuchsia_root, source)
    dest = os.path.normpath(args.dest)
    dest_abs = os.path.join(fuchsia_root, args.dest)

    if not os.path.exists(source):
        print('Source path %s does not exist within source tree' % source)
        return 1

    if os.path.exists(dest):
        print('Destination path %s already exists' % dest)
        return 1

    # create fresh branch
    if not args.no_branch:
        create_branch_for_move(source, dest, args.dry_run)

    # query build graph
    build_graph = extract_build_graph()

    # find forwarding targets to create
    forwarding_targets = find_referenced_targets(build_graph, source)

    # Set of forwarding headers to generate, identified by path
    forwarding_headers = find_externally_referenced_headers(source)

    # Libraries in garnet/public/lib use //garnet/public:config which adds
    # //garnet/public to the include path, so includes will look like
    # #include <lib/foo/...>. Look for includes of that type as well, and
    # generate forwarding headers as if these were fully qualified includes
    # if that header exists in garnet/public/lib
    garnet_public_prefix = 'garnet/public/'
    if source.startswith(garnet_public_prefix):
        relative_source = source[len(garnet_public_prefix):]
        relative_headers = find_externally_referenced_headers(relative_source)
        for relative_header in relative_headers:
            relative_path = garnet_public_prefix + relative_header
            if os.path.exists(os.path.join(fuchsia_root, relative_path)):
                forwarding_headers.add(garnet_public_prefix + relative_header)

    # search for other references to old path
    other_references = find_unknown_references(source)

    # git mv to destination
    move_directory(source, dest, args.dry_run)

    # generate forwarding targets
    write_forwarding_build_rules(
        forwarding_targets, source, dest, args.dry_run)

    # generate forwarding headers
    for header in forwarding_headers:
        generate_forwarding_header(header, source, dest, args.dry_run)

    # update references in destination directory
    update_all_references(source, dest, args.dry_run)

    # format code
    run_command(['fx', 'format-code'], args.dry_run)

    # generate commit message
    commit_message = generate_commit_message(source, dest,
                                             forwarding_targets,
                                             forwarding_headers,
                                             args.change_id)

    logging.debug(commit_message)

    # commit with message
    commit_with_message(commit_message, args.dry_run, args.no_commit)

    # log other references
    if len(other_references):
        print('%s references to old location found, please update manually' %
              len(other_references))
        for line in other_references:
            print('  %s' % line)


if __name__ == '__main__':
    sys.exit(main())
