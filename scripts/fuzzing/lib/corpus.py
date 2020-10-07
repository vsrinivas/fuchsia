#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import datetime
import errno
import subprocess


class Corpus(object):
    """Represents a corpus of fuzzing inputs.

    A fuzzing corpus is the set of "interesting" inputs as determined by the
    individual fuzzer.  See https://llvm.org/docs/LibFuzzer.html#corpus for
    details on how libFuzzer uses corpora.

    Attributes:
        fuzzer:         The Fuzzer corresponding to this object.
        nspaths:        Path in namespace where the seed and/or working corpus are stored.
        srcdir:         Host path in the source tree where the seed corpus is stored.
  """

    def __init__(self, fuzzer, label):
        self._fuzzer = fuzzer
        self._nspaths = None
        if not label:
            self._srcdir = None
            self._target = None
        elif ':' in label:
            parts = label.split(':')
            self._srcdir = parts[0]
            self._target = parts[1]
        else:
            self._srcdir = label
            self._target = os.path.basename(label)

    @property
    def fuzzer(self):
        """The Fuzzer corresponding to this object."""
        return self._fuzzer

    @property
    def buildenv(self):
        """The BuildEnv corresponding to this object."""
        return self.fuzzer.buildenv

    @property
    def host(self):
        """Alias for fuzzer.host."""
        return self.fuzzer.host

    @property
    def ns(self):
        """Alias for fuzzer.ns."""
        return self.fuzzer.ns

    @property
    def srcdir(self):
        """Corpus directory in the source tree."""
        return self._srcdir

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

    def generate_buildfile(self, build_gn=None):
        """Generates a BUILD.gn file for the seed corpus.

        Seed corpora are included in the source tree. In order to correctly update packages
        including corpora, GN needs a listing of all the files being packaged. This function can
        generate the necessary GN target on a per-fuzzer basis. More than one fuzzer may use the
        same corpus, but each must have a separate target resulting in a different path, in order
        for the GN templates to generate the component manifests correctly.

        Parameters:
            build_gn    Specifies where on the host filesystem the BUILD.gn file should be
                        generated. Defaults to the corpus source directory itself (the GN file will
                        be excluded from the list of corpus elements).

        Returns:
            The list of elements found in the corpus.
        """
        if self._srcdir:
            srcdir = self.buildenv.abspath(self._srcdir)
            target = self._target
        else:
            self.host.echo('No corpus set for {}.'.format(str(self.fuzzer)))
            self.host.echo('Please enter a path to a corpus: ', end='')
            srcdir = raw_input()
            srcdir = self.buildenv.abspath(srcdir)
            target = os.path.basename(srcdir)
        if not self.host.isdir(srcdir):
            self.host.error('No such directory: {}'.format(srcdir))

        if build_gn:
            build_gn = self.buildenv.abspath(build_gn)
            comment = '# Generated using `fx fuzz update {} -o {}`.'.format(
                str(self.fuzzer), self.buildenv.srcpath(build_gn))
        else:
            build_gn = os.path.join(srcdir, 'BUILD.gn')
            comment = '# Generated using `fx fuzz update {}`.'.format(
                str(self.fuzzer))
        build_gn_dir = os.path.dirname(build_gn)

        elems = self.host.glob(os.path.join(srcdir, '*'))
        elems = [
            os.path.relpath(elem, build_gn_dir)
            for elem in elems
            if self.host.isfile(elem) and elem != build_gn
        ]
        elems.sort()

        resource_line = 'resource("{}") {{'.format(target)
        nested_scopes = 0
        current_target_lines = []
        lines_out = []
        srcdir = self.buildenv.srcpath(srcdir)
        if self.host.isfile(build_gn):
            with self.host.open(build_gn) as gn:
                include_target = True
                for line in gn:
                    line = line.rstrip()
                    if line == resource_line:
                        # Omit the section with the matching resource() target.
                        include_target = False

                    elif line != '' or nested_scopes != 0:
                        # Delimit the file by blank lines at file scope.
                        nested_scopes += line.count('{')
                        nested_scopes -= line.count('}')
                        current_target_lines.append(line)

                    elif include_target:
                        # Include all other sections.
                        lines_out += current_target_lines
                        lines_out.append('')
                        current_target_lines = []

                    else:
                        # Matching section discarded. Reset and continue.
                        current_target_lines = []
                        include_target = True

                if include_target and current_target_lines:
                    lines_out += current_target_lines
                    lines_out.append('')

        else:
            year = datetime.datetime.now().year
            lines_out = [
                '# Copyright {} The Fuchsia Authors. All rights reserved.'.
                format(year),
                '# Use of this source code is governed by a BSD-style license that can be',
                '# found in the LICENSE file.',
                '',
                '# WARNING: AUTOGENERATED FILE. DO NOT EDIT BY HAND.',
                '',
                'import("//build/dist/resource.gni")',
                '',
            ]
        lines_out += [comment, resource_line]
        # It's not too much extra work to stay consistent with GN formatting.
        if len(elems) == 0:
            lines_out.append('  sources = []')
        elif len(elems) == 1:
            lines_out.append('  sources = [ "{}" ]'.format(elems[0]))
        else:
            lines_out.append('  sources = [')
            lines_out += ['    "{}",'.format(elem) for elem in elems]
            lines_out.append('  ]')
        lines_out += [
            '  outputs = [ "data/{}/{{{{source_file_part}}}}" ]'.format(
                srcdir[2:]),
            '}',
            '',
        ]
        with self.host.open(build_gn, 'w') as gn:
            gn.write('\n'.join(lines_out))

        if not self._srcdir and not self.fuzzer.add_corpus_to_buildfile(srcdir):
            self.host.error(
                'Failed to automatically add \'corpus = "{}"\'.'.format(srcdir),
                'Please add the corpus parameter to {} manually.'.format(
                    str(self.fuzzer)))

        return elems
