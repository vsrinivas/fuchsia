#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import contextlib
import json
import os
import shutil
import tarfile
import tempfile

from files import make_dir


class Frontend(object):
    '''Processes the contents of an SDK tarball and runs them through various
    transformation methods.

    In order to process atoms of type "foo", a frontend needs to define a
    `install_foo_atom` method that accepts a single argument representing
    the atom's metadata in JSON format.
    '''

    def __init__(self, output='', archive='', directory=''):
        self._archive = archive
        self._directory = directory
        self.output = os.path.realpath(output)
        self._source_dir = ''

    def source(self, *args):
        '''Builds a path to a source file.
        Only available while the frontend is running.
        '''
        if not self._source_dir:
            raise Exception('Error: accessing sources while inactive')
        return os.path.join(self._source_dir, *args)

    def dest(self, *args):
        '''Builds a path in the output directory.
        This method also ensures that the directory hierarchy exists in the
        output directory.
        Behaves correctly if the first argument is already within the output
        directory.
        '''
        if (os.path.commonprefix([os.path.realpath(args[0]), self.output]) ==
            self.output):
          path = os.path.join(*args)
        else:
          path = os.path.join(self.output, *args)
        return make_dir(path)

    def prepare(self, arch, atom_types):
        '''Called before elements are processed.'''
        pass

    def finalize(self, arch, atom_types):
        '''Called after all elements have been processed.'''
        pass

    def run(self):
        '''Runs this frontend through the contents of the archive.
        Returns true if successful.
        '''
        with self._create_archive_dir() as archive_dir:
            self._source_dir = archive_dir

            # Convenience for loading metadata files below.
            def load_metadata(*args):
                with open(self.source(*args), 'r') as meta_file:
                    return json.load(meta_file)
            manifest = load_metadata('meta', 'manifest.json')
            types = set([p['type'] for p in manifest['parts']])

            self.prepare(manifest['arch'], types)

            # Process each SDK atom.
            for part in manifest['parts']:
                type = part['type']
                atom = load_metadata(part['meta'])
                getattr(self, 'install_%s_atom' % type, self._handle_atom)(atom)

            self.finalize(manifest['arch'], types)

            # Reset the source directory, which may be about to disappear.
            self._source_dir = ''
        return True

    def _handle_atom(self, atom):
        '''Default atom handler.'''
        print('Ignored %s (%s)' % (atom['name'], atom['type']))

    @contextlib.contextmanager
    def _create_archive_dir(self):
        if self._directory:
            yield self._directory
        elif self._archive:
            temp_dir = tempfile.mkdtemp(prefix='fuchsia-sdk-archive')
            # Extract the tarball into the temporary directory.
            # This is vastly more efficient than accessing files one by one via
            # the tarfile API.
            with tarfile.open(self._archive) as archive:
                archive.extractall(temp_dir)
            try:
                yield temp_dir
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
        else:
            raise Exception('Error: archive or directory must be set')
