#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import argparse
import tempfile
import subprocess
import shutil
import errno

from lib.host import Host
from lib.fuzzer import Fuzzer


class Cipd(object):
    """Chrome Infra Package Deployer interface for Fuchsia fuzzing.

    Fuzzers in Fuchsia use CIPD to store and manage their corpora. CIPD
    organizes versioned, opaque blobs of data in a hierarchical namespace. For
    fuzzing, the corpus are stored at:
    https://chrome-infra-packages.appspot.com/p/fuchsia/test_data/fuzzing


    Attributes:
        corpus: The set of fuzzer inputs being managed by CIPD.
  """

    def __init__(self, corpus):
        fuzzer = corpus.fuzzer
        host = fuzzer.device.host
        self.corpus = corpus
        self._bin = host.join('.jiri_root', 'bin', 'cipd')
        self._pkg = 'fuchsia/test_data/fuzzing/' + str(fuzzer)
        self._rev = host.snapshot()

    def _cipd(self, cmd, cwd=None, quiet=False):
        """Runs a CIPD command and returns its output. Sub-classed in tests.
        If `quiet` is True, the stderr of the subprocess will be swallowed."""
        host = self.corpus.fuzzer.device.host
        p = host.create_process([self._bin] + cmd, cwd=cwd)
        if quiet:
            p.stderr = Host.DEVNULL
        return p.check_output()

    def instances(self):
        """Returns the versioned instances of a fuzzing corpus package."""
        return self._cipd(['instances', self._pkg])

    def _pkg_has_corpus(self):
        try:
            # We use `cipd instances` instead of `cipd ls` here because `ls`
            # seems to work only for package prefixes
            self._cipd(['instances', '-limit', '1', self._pkg], quiet=True)
        except subprocess.CalledProcessError:
            return False

        return True

    def install(self, label):
        """ Downloads and unpacks a CIPD package for a Fuchsia fuzzer corpus.
        Looks up version from tag.  Note if multiple versions of the package
        have the same tag (e.g. the same integration revision), this will
        select the most recent.

        Attributes:
          label: A CIPD version or tag
        """
        if not self._pkg_has_corpus():
            return False

        if ':' in label:
            try:
                output = self._cipd(['search', self._pkg, '-tag', label])
            except subprocess.CalledProcessError:
                return False
            versions = [
                x.split(':')[-1] for x in output.split('\n') if self._pkg in x
            ]
            if len(versions) == 0:
                return False
            version = versions[-1]
        else:
            version = label

        # Check that the version or ref is valid before installing.
        try:
            self._cipd(['describe', self._pkg, '-version', version])
        except subprocess.CalledProcessError:
            return False
        self._cipd(['install', self._pkg, version], cwd=self.corpus.root)
        host = self.corpus.fuzzer.device.host
        host.create_process(['chmod', '-R', '+w',
                             self.corpus.root]).check_call()
        return True

    def create(self):
        """Bundles and uploads a CIPD package for a Fuchsia fuzzer corpus."""
        pkg_def = os.path.join(self.corpus.root, 'cipd.yaml')
        with open(pkg_def, 'w') as f:
            f.write('package: ' + self._pkg + '\n')
            f.write(
                'description: Auto-generated fuzzing corpus for %s\n' %
                str(self.corpus.fuzzer))
            f.write('install_mode: copy\n')
            f.write('data:\n')
            for elem in os.listdir(self.corpus.root):
                if 'cipd' not in elem:
                    f.write('  - file: ' + elem + '\n')
        return self._cipd(
            [
                'create', '--pkg-def', pkg_def, '--ref', 'latest', '--tag',
                'integration:' + self._rev
            ])
