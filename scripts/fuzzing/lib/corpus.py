#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import errno
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
        self._nspaths = None

    @property
    def fuzzer(self):
        """The Fuzzer corresponding to this object."""
        return self._fuzzer

    @property
    def host(self):
        """Alias for fuzzer.host."""
        return self.fuzzer.host

    @property
    def ns(self):
        """Alias for fuzzer.ns."""
        return self.fuzzer.ns

    @property
    def nspaths(self):
        """List of paths to where the corpus is stored on device.

        The first element is the mutable corpus.
        """
        if not self._nspaths:
            self.find_on_device()
        return self._nspaths

    def find_on_device(self):
        data = self.ns.data('corpus')
        resource = self.ns.resource('corpus')
        self.ns.mkdir(data)
        if self.ns.ls(resource):
            self._nspaths = [data, resource]
        else:
            self._nspaths = [data]

    def add_from_host(self, pathname):
        """Copies elements from a host directory to the corpus on a device."""
        self.fuzzer.require_stopped()
        if not self.host.isdir(pathname):
            self.host.error('No such directory: {}'.format(pathname))
        pathname = os.path.join(pathname, '*')
        return self.ns.store(self.nspaths[0], pathname)

    def add_from_gcs(self, gcs_url):
        """Copies corpus elements from a GCS bucket to this corpus."""
        if not gcs_url.endswith('*'):
            gcs_url += '/*'
        with self.host.temp_dir() as temp_dir:
            cmd = ['gsutil', '-m', 'cp', gcs_url, temp_dir.pathname]
            try:
                self.host.create_process(cmd).check_call()
            except OSError as e:
                if e.errno != errno.ENOENT:
                    raise
                self.host.error(
                    'Unable to find "gsutil", which is needed to download the corpus from GCS.',
                    'You can skip downloading from GCS with the "--local" flag.'
                )
            except subprocess.CalledProcessError:
                self.host.error(
                    'Failed to download corpus from GCS.',
                    'You can skip downloading from GCS with the "--local" flag.'
                )
            return self.add_from_host(temp_dir.pathname)

    def measure(self):
        """Returns the number of corpus elements and corpus size as a pair."""
        total_num = 0
        total_size = 0
        for nspath in self.nspaths:
            sizes = self.ns.ls(nspath)
            total_num += len(sizes)
            total_size += sum(sizes.values())
        return (total_num, total_size)
