#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess


class Corpus(object):
    """Represents a corpus of fuzzing inputs.

    A fuzzing corpus is the set of "interesting" inputs as determined by the
    individual fuzzer.  See https://llvm.org/docs/LibFuzzer.html#corpus for
    details on how libFuzzer uses corpora.

    Attributes:
        fuzzer:         The Fuzzer corresponding to this object.
        nspath:         Path in namespace where the working corpus is stored.
  """

    def __init__(self, fuzzer):
        self._fuzzer = fuzzer

    @property
    def fuzzer(self):
        """The Fuzzer corresponding to this object."""
        return self._fuzzer

    @property
    def cli(self):
        """Alias for fuzzer.cli."""
        return self.fuzzer.cli

    @property
    def ns(self):
        """Alias for fuzzer.ns."""
        return self.fuzzer.ns

    @property
    def nspath(self):
        """Path in namespace where the working corpus is stored."""
        return self.ns.data('corpus')

    @property
    def inputs(self):
        """List of paths that should be passed as inputs to libFuzzer."""
        self.ns.mkdir(self.nspath)
        return [self.nspath]

    def add_from_host(self, pathname):
        """Copies elements from a host directory to the corpus on a device."""
        self.fuzzer.require_stopped()
        if not self.cli.isdir(pathname):
            self.cli.error('No such directory: {}'.format(pathname))
        pathname = os.path.join(pathname, '*')
        return self.ns.store(self.nspath, pathname)

    def add_from_gcs(self, gcs_url):
        """Copies corpus elements from a GCS bucket to this corpus."""
        if not gcs_url.endswith('*'):
            gcs_url += '/*'
        with self.cli.temp_dir() as temp_dir:
            cmd = ['gsutil', '-m', 'cp', gcs_url, temp_dir.pathname]
            try:
                self.cli.create_process(cmd).check_call()
            except subprocess.CalledProcessError:
                self.cli.error(
                    'Failed to download corpus from GCS.',
                    'You can skip downloading from GCS with the "--local" flag.'
                )
            return self.add_from_host(temp_dir.pathname)

    def measure(self):
        """Returns the number of corpus elements and corpus size as a pair."""
        sizes = self.ns.ls(self.nspath)
        return (len(sizes), sum(sizes.values()))
