#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import argparse
import itertools
import json
import os
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPTS_DIR = os.path.dirname(SCRIPT_DIR)
FUCHSIA_ROOT = os.path.dirname(SCRIPTS_DIR)

# Amount of file size change allowed, expressed as a percentage of the original
# file size.
SIZE_SHIFT_THRESHOLD_PERCENT = 1


class Type(object):
    AUX = 'aux'
    IMAGE = 'image'
    TESTS = 'tests'
    @classmethod
    def all(cls): return [cls.AUX, cls.IMAGE, cls.TESTS]


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
    '''A JSON encoder that handles sets and sorts lists.'''

    def default(self, object):
        if isinstance(object, FileDataSet) or isinstance(object, FileData):
            return object.to_json()
        return json.JSONEncoder.default(self, object)


class FileData(object):
    '''Represents a file referred to in a manifest.'''

    def __init__(self, path, size=None):
        self.path = path
        self.size = size if size else os.path.getsize(path)

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
        }

    @classmethod
    def from_json(cls, input):
        return FileData(input['path'], input['size'])


class FileDataSet(object):
    '''Represents a set of files.'''

    def __init__(self):
        # map { name --> FileData }
        self.files = {}

    def add(self, name, file):
        if name in self.files and file != self.files[name]:
            print('Error: different file under path ' + name + ':')
            print(' - ' + file)
            print(' - ' + self.files[name])
            return False
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


class Summary(object):
    '''Data for a particular state of the build.'''

    def __init__(self):
        # map { type --> FileDataSet }
        self.objects = {}

    def add_objects(self, type, objects):
        dataset = self.objects.setdefault(type, FileDataSet())
        for name, path in objects.iteritems():
            dataset.add(name, FileData(path))

    def get_objects(self, type):
        return self.objects[type]

    def __repr__(self):
        items = ['%s=%s' % (t, len(o)) for (t, o) in self.objects.iteritems()]
        return 'S[' + ', '.join(items) + ']'

    def to_json(self, output):
        json.dump(self.objects, output, cls=CustomJSONEncoder, indent=2,
                  sort_keys=True, separators=(',', ': '))

    @classmethod
    def from_json(cls, input):
        result = Summary()
        data = json.load(input)
        for type in Type.all():
            result.objects[type] = FileDataSet.from_json(data[type])
        return result


def generate_summary(manifests, base_dir):
    '''Generates a summary based on the manifests found in the build.'''
    result = Summary()
    for type in Type.all():
        for manifest in filter(lambda m: m.type == type, manifests):
            contents = manifest.contents.copy()
            contents = dict([(n, os.path.join(base_dir, p))
                             for (n, p) in contents.iteritems()])
            result.add_objects(type, contents)
    return result


def compare_summaries(reference, current):
    '''Compares summaries for two states of the build.'''
    match = True
    for type in Type.all():
        reference_objects = reference.get_objects(type)
        current_objects = current.get_objects(type)
        reference_names = reference_objects.filenames()
        current_names = current_objects.filenames()

        # Missing and new files.
        if reference_names != current_names:
            match = False
            print(('-------[ ' + type + ' ]').ljust(32, '-'))
            removed = reference_names - current_names
            if removed:
                print('Elements that have been removed:')
                for element in removed:
                    print(' - ' + element)
            added = reference_names - current_names
            if added:
                print('Elements that have been added:')
                for element in added:
                    print(' - ' + element)

        # Size changes.
        for name in reference_names & current_names:
            reference_size = reference_objects.get_file(name).size
            current_size = current_objects.get_file(name).size
            if current_size == reference_size:
                continue
            diff_percentage = 100 * (current_size - reference_size) / reference_size
            if abs(diff_percentage) >= SIZE_SHIFT_THRESHOLD_PERCENT:
                match = False
                print('Error: size change for ' + name + ': ' +
                      str(diff_percentage) + '%')

    return match


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
            path = os.path.join(args.build_dir, 'obj', 'build', 'unification',
                                'images',
                                '%s-%s.unification.manifest' % (origin, type))
            with open(path, 'r') as manifest_file:
                contents = dict(map(lambda line: line.strip().split('=', 1),
                                     manifest_file.readlines()))
                manifests.append(Manifest(origin, type, contents))

    # Generate a summary for the current build.
    summary = generate_summary(manifests, args.build_dir)

    # If applicable, save the current build's summary.
    if args.summary:
        with open(args.summary, 'w') as output_file:
            summary.to_json(output_file)

    # If applicable, compare the current summary to a previously-saved one.
    if args.reference:
        with open(args.reference, 'r') as input_file:
            reference = Summary.from_json(input_file)
            if not compare_summaries(reference, summary):
                print('Error: summaries do not match!')
                return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
