#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import json
import os
import shutil
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    os.path.dirname(             # sdk
    os.path.abspath(__file__))))

sys.path += [os.path.join(FUCHSIA_ROOT, 'build', 'sdk')]
from sdk_common import Atom


class Metadata(object):
    ''''Represents the SDK's metadata.'''

    def __init__(self, metadata):
        self.target_arch = metadata['target-arch']
        self.host_arch = metadata['host-arch']


class Builder(object):
    '''Processes atoms found in a manifest.

    Domains that this builder handles are set via the `domains` constructor
    argument.
    In order to process atoms for a domain "foo", a builder needs to define a
    `install_foo_atom` method that accepts a single Atom as an argument.
    '''

    def __init__(self, domains=[]):
        self.domains = domains
        self.metadata = None

    def prepare(self):
        '''Called before atoms are processed.'''
        pass

    def finalize(self):
        '''Called after all atoms have been processed.'''
        pass

    def make_dir(self, file_path):
        '''Creates the directory hierarchy for the given file.'''
        target = os.path.dirname(file_path)
        try:
            os.makedirs(target)
        except OSError as exception:
            if exception.errno == errno.EEXIST and os.path.isdir(target):
                pass
            else:
                raise


def process_manifest(manifest, builder):
    '''Reads an SDK manifest and passes its findings to a builder.'''

    # Read the contents of the manifest file.
    with open(manifest, 'r') as manifest_file:
        manifest_data = json.load(manifest_file)
    return _process_manifest_data(manifest_data, builder)


def _process_manifest_data(manifest, builder):
    '''For testing.'''
    atoms = [Atom(a) for a in manifest['atoms']]
    builder.metadata = Metadata(manifest['meta'])

    # Verify that the manifest only contains supported domains.
    if builder.domains:
        extra_domains = set(filter(lambda d: d not in builder.domains,
                                   [a.id.domain for a in atoms]))
        if extra_domains:
            print('The following domains are not currently supported: %s' %
                  ', '.join(extra_domains))
            return False

    builder.prepare()

    # Pass the various atoms through the builder.
    for atom in atoms:
        domain = atom.id.domain
        getattr(builder, 'install_%s_atom' % domain)(atom)

    # Wrap things up.
    builder.finalize()

    return True
