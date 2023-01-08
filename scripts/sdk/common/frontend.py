#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Base class for generating SDKs from the Fuchsia IDK.

This base class accepts a directory or tarball of an instance of the Integrator
Developer Kit (IDK)
and uses the metadata from the IDK to drive the construction of a specific SDK.
"""

import contextlib
import json
import os
import shutil
import sys
import tarfile
import tempfile

from files import make_dir

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(  # scripts
        os.path.dirname(  # sdk
            _SCRIPT_DIR)))  # common

sys.path += [os.path.join(_FUCHSIA_ROOT, 'third_party', 'mako')]
from mako.lookup import TemplateLookup
from mako.template import Template


class Frontend(object):
    """Processes the IDK by runnings them through various transformation methods.

    In order to process atoms of type "foo", a frontend needs to define a
    `install_foo_atom` method that accepts a single argument representing
    the atom's metadata in JSON format.
  """

    def __init__(self, output='', archive='', directory='', local_dir=''):
        """Initializes a Frontend instance.

    Note that only one of archive or directory should be specified.

    Args:
      output: The output directory. The contents of this directory are removed
        before processing the IDK.
      archive: The tarball archive to process. Can be empty meaning use the
        directory parameter as input.
      directory: The directory containing an unpackaged IDK. Can be empty
        meaning use the archive parameter as input.
      local_dir: The local directory used to find additional resources used
        during the transformation.
    """
        self._archive = archive
        self._directory = directory
        self._local_dir = local_dir
        self.output = os.path.realpath(output)
        self._source_dir = ''

    def source(self, *args):
        """Builds a path to a source file.

    Only available while the frontend is running.

    Args:
      *args: the collection of path elements to join

    Returns:
      The path string to the file.

    Raises:
      Exception: if this method is called when not processing the IDK or if
      source_dir was empty.
    """
        if not self._source_dir:
            raise Exception('Error: accessing sources while inactive')
        return os.path.join(self._source_dir, *args)

    def dest(self, *args):
        """Builds a path in the output directory.

        This method also ensures that the directory hierarchy exists in the
        output directory.
        Behaves correctly if the first argument is already within the output
        directory.

    Args:
      *args: the collection of path elements to joing.

    Returns:
      The path string to the file.
    """
        if (os.path.commonprefix([os.path.realpath(args[0]),
                                  self.output]) == self.output):
            path = os.path.join(*args)
        else:
            path = os.path.join(self.output, *args)
        return make_dir(path)

    def local(self, *args):
        """Builds a path in the local directory.

    Args:
      *args: the collection of path elements to joing.

    Returns:
      The path string to the file.
    """
        return os.path.join(self._local_dir, *args)

    def copy_file(
            self,
            filename,
            root='',
            destination='',
            result=[],
            allow_overwrite=True):
        """Copies a file from a given root directory with a collector.

       Copies the file from the root directory into the same path in the
       destination directory.

        If result is not None, the relative path to the file is added to the
        list.
    Args:
      filename: The path to a file in the root directory.
      root: The root directory used to calcualte a relative path to the file.
      destination: The destination root directory.
      result: A collector list if not None, has the relative path of the file
        appended to the list.
      allow_overwrite: Whether to allow the destination file to be overwritten.

    Raises:
      Exception: If the path in file is not within the root directory.
      Exception: If allow_overwrite is False and the destination file exists.
    """
        if os.path.commonprefix([root, filename]) != root:
            raise Exception('%s is not within %s' % (filename, root))
        relative_path = os.path.relpath(filename, root)
        dest = self.dest(destination, relative_path)
        if not allow_overwrite and os.path.exists(dest):
            raise Exception(
                'Attempt to overwrite file: %s -> %s' % (filename, dest))
        shutil.copy2(self.source(filename), dest)
        result.append(relative_path)

    def copy_files(
            self,
            files,
            root='',
            destination='',
            result=[],
            allow_overwrite=True):
        """Copies files from a given root directory with a collector.

       This is done by calling copy_file() iteratively.

        If result is not None, the relative path to the file is added to the
        list.
    Args:
      files: The path to a file in the root directory.
      root: The root directory used to calcualte a relative path to the file.
      destination: The destination root directory.
      result: A collector list if not None, has the relative path of the file
        appended to the list.
      allow_overwrite: Whether to allow destination files to be overwritten.
    """
        for f in files:
            self.copy_file(f, root, destination, result, allow_overwrite)

    def write_file(self, path, template_name, data):
        """Writes a file based on a Mako template.

    The templates are found in the local directory named templates.

    Args:
      path: The output file path.
      template_name: The name of the Mako template without the '.mako'
        extension.
      data: The data model used to render the template.
    """
        lookup = TemplateLookup(directories=[self.local('templates')])
        template = lookup.get_template(template_name + '.mako')
        with open(path, 'w') as outfile:
            outfile.write(template.render(data=data))

    def prepare(self, arch, atom_types):
        """Called before elements are processed."""
        pass

    def finalize(self, arch, atom_types):
        """Called after all elements have been processed."""
        pass

    def run(self):
        """Runs this frontend through the contents of the archive.

        Returns true if successful.
        """
        with self._create_archive_dir() as archive_dir:
            self._source_dir = archive_dir

            manifest = load_metadata(self.source('meta', 'manifest.json'))
            if not manifest:
                return False
            types = set([p['type'] for p in manifest['parts']])

            try:
                self.prepare(manifest['arch'], types)
            except Exception as e:
                print('Failed preparing elements: %s' % e)
                return False

            # Process each SDK atom.
            for part in manifest['parts']:
                type = part['type']
                atom = load_metadata(self.source(part['meta']))
                if not atom:
                    return False
                # 'version_history' is a versioned sdk atom that doesn't have a meta.json.
                if type == 'version_history':
                    getattr(self, 'install_%s_atom' % type,
                            self._handle_atom)(atom, part)
                    continue
                getattr(self, 'install_%s_atom' % type, self._handle_atom)(atom)

            self.finalize(manifest['arch'], types)

            # Reset the source directory, which may be about to disappear.
            self._source_dir = ''
        return True

    def get_atom_type(self, atom):
        '''Returns the SDK atom type.'''
        # For versioned SDK atoms, the type is inside the data field.
        if 'schema_id' in atom:
            return atom['data']['type']
        return atom['type']

    def get_atom_name(self, atom):
        '''Returns the SDK atom name.'''
        # For versioned SDK atoms, the name is inside the data field.
        if 'schema_id' in atom:
            return atom['data']['name']
        return atom['name']

    def _handle_atom(self, atom, manifest=None):
        """Default atom handler."""
        print(
            'Ignored %s (%s)' %
            (self.get_atom_name(atom), self.get_atom_type(atom)))

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
                def is_within_directory(directory, target):
                    
                    abs_directory = os.path.abspath(directory)
                    abs_target = os.path.abspath(target)
                
                    prefix = os.path.commonprefix([abs_directory, abs_target])
                    
                    return prefix == abs_directory
                
                def safe_extract(tar, path=".", members=None, *, numeric_owner=False):
                
                    for member in tar.getmembers():
                        member_path = os.path.join(path, member.name)
                        if not is_within_directory(path, member_path):
                            raise Exception("Attempted Path Traversal in Tar File")
                
                    tar.extractall(path, members, numeric_owner=numeric_owner) 
                    
                
                safe_extract(archive, temp_dir)
            try:
                yield temp_dir
            finally:
                shutil.rmtree(temp_dir, ignore_errors=True)
        else:
            raise Exception('Error: archive or directory must be set')


def load_metadata(filename):
    """Loads metadata from a file.
    Args:
      filename: The absolute path to the file to load metadata from.

    Returns:
      A dictionary representing the metadata from the file.
      Returns False if the file doesn't exist or is invalid JSON.
    """
    try:
        with open(filename, 'r') as meta_file:
            return json.load(meta_file)
    except IOError:
        print('Metadata file not found: %s' % filename)
        return False
    except ValueError as e:
        print('Unable to parse %s: %s' % (filename, e))
        return False
