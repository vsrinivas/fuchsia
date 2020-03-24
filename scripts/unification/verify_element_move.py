#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import argparse
import itertools
import json
import os
import re
import sys

from common import (FUCHSIA_ROOT, run_command)

sys.path.append(os.path.join(FUCHSIA_ROOT, 'build', 'images'))
import elfinfo


# The maximum number of size percentage points a binary is allowed to drop.
# A greater amount will raise a flag.
MAX_SIZE_DECREASE = 10

# The maximum number of size percentage points a binary is allowed to gain.
# A greater amount will raise a flag.
MAX_SIZE_INCREASE = 1


class TypeBody(object):
    def __init__(self, name, is_manifest):
        self.name = name
        self.is_manifest = is_manifest

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        return self.name == other.name

    def __str__(self):
        return self.name

    def __repr__(self):
        return 'T[%s]' % self.name

    def load_data(self, build_dir, origin):
        if self.is_manifest:
            path = os.path.join(build_dir, 'obj', 'build', 'unification',
                                'images',
                                '%s-%s.unification.manifest' % (origin, self.name))
            with open(path, 'r') as manifest_file:
                contents = dict(map(lambda line: line.strip().split('=', 1),
                                     manifest_file.readlines()))
                contents = dict([(k, os.path.join(build_dir, v))
                                 for k, v in contents.iteritems()])
                return Manifest(origin, self, contents)
        elif self.name == 'fuzzers':
            path = os.path.join(build_dir, '%s_zircon_fuzzers.json' % origin)
            with open(path, 'r') as manifest_file:
                contents = json.load(manifest_file)
            fuzzers = list(set([i.replace('-fuzzer.asan-ubsan', '')
                                 .replace('-fuzzer.asan', '')
                                 .replace('-fuzzer.ubsan', '')
                                for i in contents]))
            return Manifest(origin, self, fuzzers)
        elif self.name == 'host_tests':
            path = os.path.join(build_dir,
                                '%s_zircon_host_tests.json' % origin)
            with open(path, 'r') as manifest_file:
                contents = json.load(manifest_file)
                return Manifest(origin, self, contents)
        elif self.name == 'zbi_tests':
            if origin == Origin.LEGACY:
                path = os.path.join(build_dir, 'zbi_tests.json')
                with open(path, 'r') as manifest_file:
                    contents = json.load(manifest_file)
                tests = [ZbiTest.extract(build_dir, t) for t in contents]
            else:
                # The .json file is already the aggregation of both types of
                # tests.
                tests = []
            return Manifest(origin, self, tests)
        raise Exception('Unhandled type: ' + self)

    def to_json(self):
        return self.name


class Type(object):
    AUX = TypeBody('aux', True)
    FUZZERS = TypeBody('fuzzers', False)
    HOST_TESTS = TypeBody('host_tests', False)
    IMAGE = TypeBody('image', True)
    TESTS = TypeBody('tests', True)
    ZBI_TESTS = TypeBody('zbi_tests', False)

    @classmethod
    def all(cls): return [cls.AUX, cls.FUZZERS, cls.HOST_TESTS, cls.IMAGE,
                          cls.TESTS, cls.ZBI_TESTS]

    @classmethod
    def manifests(cls): return [t for t in cls.all() if t.is_manifest]


class Origin(object):
    LEGACY = 'legacy'
    MIGRATED = 'migrated'
    @classmethod
    def all(cls): return [cls.LEGACY, cls.MIGRATED]


class Manifest(object):
    '''Lists the contents of a manifest file'''

    def __init__(self, origin, type, contents):
        self.origin = origin
        self.type = type
        self.contents = contents

    def __repr__(self):
        return 'M[%s-%s]' % (self.origin, self.type)


class CustomJSONEncoder(json.JSONEncoder):

    def default(self, object):
        if (isinstance(object, FileDataSet) or
            isinstance(object, FileData) or
            isinstance(object, TypeBody) or
            isinstance(object, ZbiTest)):
            return object.to_json()
        return json.JSONEncoder.default(self, object)


class FileData(object):
    '''Represents a file referred to in a manifest.'''

    def __init__(self, path, size=None, libs=None):
        self.path = path
        self.size = size if size else os.path.getsize(path)
        if libs != None:
            self.libs = set(libs)
        else:
            info = elfinfo.get_elf_info(path)
            self.libs = info.needed if info else set()

    def __eq__(self, other):
        return self.path == other.path

    def __ne__(self, other):
        return not self.__eq__(other)

    def __hash__(self):
        return len(hash(self.path))

    def __repr__(self):
        return 'F[' + self.path + ']'

    def to_json(self):
        return {
            'path': self.path,
            'size': self.size,
            'libs': sorted(self.libs),
        }

    @classmethod
    def from_json(cls, input):
        return FileData(input['path'], input['size'], input['libs'])


class FileDataSet(object):
    '''Represents a set of files.'''

    def __init__(self):
        # map { name --> FileData }
        self.files = {}

    def add(self, name, file):
        if os.path.basename(name) == 'libdriver.so':
            # libdriver is a complicated hydra whose many heads we don't need to
            # worry about here.
            return
        if name in self.files and file != self.files[name]:
            print('Error: different file under path ' + name + ':')
            print(' - ' + str(file))
            print(' - ' + str(self.files[name]))
            return
        self.files[name] = file

    def filenames(self):
        return set(self.files.keys())

    def get_file(self, name):
        return self.files[name]

    def __len__(self):
        return len(self.files)

    def to_json(self):
        return self.files

    @classmethod
    def from_json(cls, input):
        result = FileDataSet()
        for name, data in input.iteritems():
            result.add(name, FileData.from_json(data))
        return result


class ZbiTest(object):

    def __init__(self, name, bootfs, cmdline, extras):
        self.name = name
        self.bootfs = sorted(bootfs)
        self.cmdline = sorted(cmdline)
        self.extras = extras

    def __eq__(self, other):
        return (self.name == other.name and
                self.bootfs == other.bootfs and
                self.cmdline == other.cmdline and
                self.extras == other.extras)

    def __ne__(self, other):
        return not self.__eq__(other)

    def __repr__(self):
        return 'Z[' + self.name + ']'

    def __hash__(self):
        return hash((self.name,
                     tuple(b for b in self.bootfs),
                     tuple(c for c in self.cmdline),
                     json.dumps(self.extras, sort_keys=True)))

    def diff_bootfs(self, other):
        removed = set(self.bootfs) - set(other.bootfs)
        added = set(other.bootfs) - set(self.bootfs)
        return removed, added

    def diff_cmdline(self, other):
        removed = set(self.cmdline) - set(other.cmdline)
        added = set(other.cmdline) - set(self.cmdline)
        return removed, added

    def to_json(self):
        return {
            'name': self.name,
            'bootfs': self.bootfs,
            'cmdline': self.cmdline,
            'extras': self.extras,
        }

    @classmethod
    def from_json(cls, input):
        return ZbiTest(input['name'], input['bootfs'], input['cmdline'],
                       input['extras'])

    @classmethod
    def extract(cls, build_dir, data):
        zbi_path = os.path.join(build_dir, data['path'])
        zbi_tool = os.path.join(build_dir, 'host_x64', 'zbi')
        contents = run_command([zbi_tool, '-tv', zbi_path])
        bootfs, cmdline = cls._parse_zbi(contents)
        extras = dict((i, data[i]) for i in ['bootserver_netboot', 'cpu',
                                             'device_types', 'disabled',
                                             'success_string'])
        return ZbiTest(data['name'], bootfs, cmdline, extras)

    @classmethod
    def _parse_zbi(cls, data):
        bootfs = []
        cmdline = []
        current_header = None
        for line in data.splitlines():
            header_match = re.match('^[0-9a-f]{8}: [0-9a-f]{8} (\w+).*$', line)
            if header_match:
                current_header = header_match.group(1)
                continue
            if current_header == 'BOOTFS':
                bootfs_match = re.match('^\s{8}: [0-9a-f]{8} [0-9a-f]{8} (.+)$',
                                        line)
                if bootfs_match:
                    bootfs.append(bootfs_match.group(1))
            elif current_header == 'CMDLINE':
                cmd_match = re.match('^\s{8}: ([^\s]+)$', line)
                if cmd_match:
                    cmdline.append(cmd_match.group(1))
        return (bootfs, cmdline)


class Summary(object):
    '''Data for a particular state of the build.'''

    def __init__(self):
        # map { type --> FileDataSet | list }
        self.objects = {}

    def add_objects(self, type, objects):
        if isinstance(objects, dict):
            dataset = self.objects.setdefault(type, FileDataSet())
            for name, path in objects.iteritems():
                dataset.add(name, FileData(path))
        elif isinstance(objects, list):
            dataset = self.objects.setdefault(type, [])
            self.objects[type] = sorted(set(dataset + objects))

    def get_objects(self, type):
        return self.objects[type]

    def __repr__(self):
        items = ['%s=%s' % (t, len(o)) for (t, o) in self.objects.iteritems()]
        return 'S[' + ', '.join(items) + ']'

    def to_json(self, output):
        data = dict([(str(k), v) for k, v in self.objects.iteritems()])
        json.dump(data, output, cls=CustomJSONEncoder, indent=2,
                  sort_keys=True, separators=(',', ': '))

    @classmethod
    def from_json(cls, input):
        result = Summary()
        data = json.load(input)
        for type in Type.manifests():
            result.objects[type] = FileDataSet.from_json(data[str(type)])
        for type in [Type.HOST_TESTS, Type.FUZZERS]:
            result.objects[type] = data[str(type)]
        result.objects[Type.ZBI_TESTS] = [ZbiTest.from_json(t)
                                          for t in data[str(Type.ZBI_TESTS)]]
        return result


def generate_summary(manifests):
    '''Generates a summary based on the manifests found in the build.'''
    result = Summary()
    for type in Type.all():
        for manifest in filter(lambda m: m.type == type, manifests):
            result.add_objects(type, manifest.contents)
    return result


def report(manifest, is_error, message):
    type = 'Error' if is_error else 'Warning'
    print('%s%s%s' % (type.ljust(10), str(manifest).ljust(12), message))


def print_size(value):
    for unit in ['B', 'K', 'M', 'G']:
        if abs(value) < 1024.0:
            return '%3.1f%s' % (value, unit)
        value /= 1024.0
    return '%.1f%s' % (value, 'T')


def compare_summaries(reference, current):
    '''Compares summaries for two states of the build.'''
    has_errors = False
    has_warnings = False
    all_fuzzers_present = True  # Should be a list of changed fuzzers, really

    # Fuzzers
    reference_fuzzers = set(reference.get_objects(Type.FUZZERS))
    current_fuzzers = set(current.get_objects(Type.FUZZERS))
    if reference_fuzzers != current_fuzzers:
        all_fuzzers_present = False
        has_errors = True
        for fuzzer in reference_fuzzers - current_fuzzers:
            report(Type.FUZZERS, True, 'fuzzer removed: ' + fuzzer)
        for fuzzer in current_fuzzers - reference_fuzzers:
            report(Type.FUZZERS, True, 'fuzzer added: ' + fuzzer)

    for type in Type.manifests():
        reference_objects = reference.get_objects(type)
        current_objects = current.get_objects(type)
        reference_names = reference_objects.filenames()
        current_names = current_objects.filenames()

        # Missing and new files.
        if reference_names != current_names:
            for element in reference_names - current_names:
                if (re.match('^bin/.+-fuzzer\..{1,7}san$', element) or
                    re.match('^meta/.+-fuzzer\..{1,7}san\.cmx$', element)):
                    is_error = False
                    has_warnings = True
                else:
                    is_error = True
                    has_errors = True
                report(type, is_error, 'element removed: ' + element)
            for element in current_names - reference_names:
                has_errors = True
                report(type, True, 'element added: ' + element)

        # Size changes.
        for name in reference_names & current_names:
            reference_size = reference_objects.get_file(name).size
            current_size = current_objects.get_file(name).size
            if current_size == reference_size:
                continue
            is_diff_positive = current_size > reference_size
            diff_percentage = 100 * (current_size - reference_size) / reference_size
            if (diff_percentage < -MAX_SIZE_DECREASE or
                diff_percentage > MAX_SIZE_INCREASE):
                has_errors = True
                is_error = True
            else:
                has_warnings = True
                is_error = False
            report(type, is_error, 'size change for ' + name + ': ' +
                   ('+' if is_diff_positive else '-') +
                   str(abs(diff_percentage)) + '% (' +
                   print_size(current_size) + ')')

        # Linking changes.
        for name in reference_names & current_names:
            reference_libs = reference_objects.get_file(name).libs
            current_libs = current_objects.get_file(name).libs
            if current_libs == reference_libs:
                continue
            for lib in reference_libs - current_libs:
                has_errors = True
                report(type, True, 'shared library removed from ' + name +
                       ': ' + lib)
            for lib in current_libs - reference_libs:
                if (lib == 'libc++abi.so.1' or
                    lib == 'libdevmgr-launcher.so' or
                    lib == 'libdevmgr-integration-test.so' or
                    lib == 'libdriver-integration-test.so'):
                    is_error = False
                    has_warnings = True
                else:
                    is_error = True
                    has_errors = True
                report(type, is_error, 'shared library added to ' + name +
                       ': ' + lib)

    # Host tests.
    reference_host_tests = set(reference.get_objects(Type.HOST_TESTS))
    current_host_tests = set(current.get_objects(Type.HOST_TESTS))
    if reference_host_tests != current_host_tests:
        has_errors = True
        for element in reference_host_tests - current_host_tests:
            report(Type.HOST_TESTS, True, 'test removed: ' + element)
        for element in current_host_tests - reference_host_tests:
            report(Type.HOST_TESTS, True, 'test added: ' + element)

    # ZBI tests.
    reference_zbi_tests = dict((t.name, t)
                               for t in reference.get_objects(Type.ZBI_TESTS))
    current_zbi_tests = dict((t.name, t)
                             for t in current.get_objects(Type.ZBI_TESTS))
    reference_zbi_names = set(reference_zbi_tests.keys())
    current_zbi_names = set(current_zbi_tests.keys())
    if reference_zbi_names != current_zbi_names:
        has_errors = True
        for element in reference_zbi_names - current_zbi_names:
            report(Type.ZBI_TESTS, True, 'test removed: ' + element)
        for element in current_zbi_names - reference_zbi_names:
            report(Type.ZBI_TESTS, True, 'test added: ' + element)
    for name in reference_zbi_names & current_zbi_names:
        reference_test = reference_zbi_tests[name]
        current_test = current_zbi_tests[name]
        if reference_test == current_test:
            continue
        removed, added = reference_test.diff_bootfs(current_test)
        if removed:
            has_errors = True
            for element in removed:
                report(Type.ZBI_TESTS, True,
                       'removed from ' + name + ' bootfs: ' + element)
        if added:
            has_errors = True
            for element in added:
                report(Type.ZBI_TESTS, True,
                       'added to ' + name + ' bootfs: ' + element)
        removed, added = reference_test.diff_cmdline(current_test)
        if removed:
            has_errors = True
            for element in removed:
                report(Type.ZBI_TESTS, True,
                       'removed from ' + name + ' cmdline: ' + element)
        if added:
            has_errors = True
            for element in added:
                report(Type.ZBI_TESTS, True,
                       'added to ' + name + ' cmdline: ' + element)
        if reference_test.extras != current_test.extras:
            report(Type.ZBI_TESTS, True, 'different params for ' + name + ': ' +
                                         json.dumps(reference_test.extras) +
                                         ' vs. ' +
                                         json.dumps(current_test.extras))

    if has_errors:
        print('Error: summaries do not match!')
        return False
    if not has_warnings:
        print('<none>')
    return True


def main():
    parser = argparse.ArgumentParser(
            description='Performs verifications after moving an element from '
                        'ZN to GN.')
    parser.add_argument('--build-dir',
                        help='path to the GN build dir',
                        default=os.path.join(FUCHSIA_ROOT, 'out', 'default'))
    parser.add_argument('--summary',
                        help='path to the summary file to generate')
    parser.add_argument('--reference',
                        help='path to the summary file to compare against')
    args = parser.parse_args()

    if not args.summary and not args.reference:
        print('At least one of --summary or --reference needs to be set.')
        parser.print_help()
        return 1

    # Load up manifests from the current build.
    manifests = []
    for origin in Origin.all():
        for type in Type.all():
            manifests.append(type.load_data(args.build_dir, origin))

    # Generate a summary for the current build.
    summary = generate_summary(manifests)

    # If applicable, save the current build's summary.
    if args.summary:
        dirname = os.path.dirname(args.summary)
        if not os.path.exists(dirname):
            os.makedirs(dirname)
        with open(args.summary, 'w') as output_file:
            summary.to_json(output_file)

    # If applicable, compare the current summary to a previously-saved one.
    if args.reference:
        with open(args.reference, 'r') as input_file:
            reference = Summary.from_json(input_file)
            if not compare_summaries(reference, summary):
                return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
