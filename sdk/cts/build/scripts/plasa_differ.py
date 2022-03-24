#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys
import json
import subprocess
from dataclasses import dataclass
from typing import List


@dataclass
class Fragment:
    """PlaSA Fragment metadata.

    All fields in this data class (save for the "cts_path" field) is pulled
    directly from the PlaSA manifest file.

    "cts_path" points to the location of this fragment file on the host
    machine.
    """
    file: str
    kind: str
    path: str
    dest: str
    cts_path: str


@dataclass
class Match:
    """Data class used to match corresponding fragments between manifest files.

    This also stores match result information in the form of an error message
    and a compatibility status bit.
    """
    before: Fragment
    after: Fragment
    message: str
    is_compatible: bool

    def get_message(self):
        """Small helper method to make the output more useful info for errors.

        Returns:
            A string representing the Fragment key and match error message.
        """
        return '{}\n{}'.format(self.before.path, self.message)


@dataclass
class PlasaDiffer:
    """Class that actually performs the PlaSA manifest diffing operation.

    Throws an exception if a compatibility issue is detected, or some other
    processing error occurs.
    """
    before_manifest: str
    after_manifest: str
    kinds: List[str]
    utils_dir: str
    out_dir: str

    def __post_init__(self):
        if not self.before_manifest:
            raise ('before manifest must not be empty.')
        if not self.after_manifest:
            raise ('after manifest must not be empty.')
        if self.kinds == None or len(self.kinds) == 0:
            raise ValueError('kinds list cannot be None or empty.')
        if not os.path.exists(self.utils_dir):
            raise ValueError('utils_dir path cannot be empty and must exist.')
        if not os.path.exists(self.out_dir):
            raise ValueError('out_dir path cannot be empty and must exist.')

    def load_manifest(self, manifest):
        """Load the PlaSA Manifest JSON file into a dictionary.

        The 'path' field of each fragment acts as a unique key, and
        allow us to map to the same fragment in other PlaSA manifest files.

        Returns:
            A dict mapping the fragment 'path' field to the fragment object.
        """
        fragments_root = os.path.dirname(manifest)
        d = {}
        with open(manifest) as f:
            data = json.load(f)
            for entry in data:
                # We may want to run this script in separate processes to allow
                # parallel verification. Filter out fragment types that we
                # don't care about in this run.
                if entry['kind'] in self.kinds:
                    cts_path = os.path.join(fragments_root, entry['path'])
                    if not os.path.exists(cts_path):
                        raise ValueError(
                            'Fragment path {} does not exist'.format(cts_path))
                    entry['cts_path'] = cts_path
                    d[entry['path']] = Fragment(**entry)
        return d

    def diff_fragments(self, before, after):
        """Diff two fragment files against each other.

        Returns:
            A string message containing any useful output of the diff tool.
            A boolean value: True if the two fragment interfaces are compatible.
        """
        if before.kind != after.kind:
            return 'Left fragment kind ({}) does not match Right fragment kind ({}).'.format(
                before.kind, after.kind), False

        if before.kind == 'api_fidl':
            return self._diff_fidl(before, after)
        else:
            return 'Diffing for fragments of type {} is not yet supported.'.format(
                before.kind), False

    def _diff_fidl(self, before, after):
        """Diff two api_fidl fragment files against each other.

        The above method calls this method to diff FIDL fragment files.
        """

        tool = os.path.join(self.utils_dir, 'fidl_api_diff')
        out_file = os.path.join(self.out_dir, os.path.basename(before.cts_path))
        args = [
            tool,
            '--before-file',
            before.cts_path,
            '--after-file',
            after.cts_path,
            '--api-diff-file',
            out_file,
        ]

        try:
            p = subprocess.run(args, check=True, capture_output=True, text=True)
            # TODO(jcecil): Make fidl_api_diff throw an exception
            # when API breaking changes are detected.
            with open(out_file, 'r') as result:
                data = result.read()
                result.close()
            if 'APIBreaking' in data:
                return data, False
            return data, True

        except subprocess.CalledProcessError as e:
            message = 'Return Code {}'.format(e.returncode)
            if e.output:
                message += "\n{}".format(e.output)
            if e.stdout:
                message += "\n{}".format(e.stdout)
            if e.stderr:
                message += "\n{}".format(e.stderr)
            return message, False

    def run(self):
        """Diff two PlaSA manifest files.

        Raises:
            RuntimeError if incompatible changes are detected.
        """

        before_fragments = self.load_manifest(self.before_manifest)
        after_fragments = self.load_manifest(self.after_manifest)

        matches = {}

        # Match fragments from the before and after PlaSA manifest files,
        # and diff them using the provided tools.
        # Delete them from the dictionaries so we can detect discrepancies.
        for key, before in list(before_fragments.items()):
            del before_fragments[key]

            try:
                after = after_fragments[key]
                del after_fragments[key]
                message, is_compatible = self.diff_fragments(before, after)

            except KeyError:
                after = None
                message = "PlaSA element was deleted."
                is_compatible = False

            finally:
                matches[key] = Match(before, after, message, is_compatible)

        # PlaSA elements that are newly introduced in the after manifest
        # are identified and captured here.
        for key, after in list(after_fragments.items()):
            del after_fragments[key]
            before = None
            message = "PlaSA element was added."

            # TODO(jcecil): Consider flagging new additions to the PlaSA
            # manifest here, to ensure they have sufficient CTS test coverage.
            is_compatible = True

            matches[key] = Match(before, after, message, is_compatible)

        issues = ""
        found_issue = False
        for m in matches.values():
            if not m.is_compatible:
                issues = '{}\n-----\n{}'.format(issues, m.get_message())
                found_issue = True

        if found_issue:
            raise RuntimeError(
                'Found compatibility issue(s):\n\n{}'.format(issues))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--before_manifest',
        help='Path to the old PlaSA manifest file, from the CTS release.',
        required=True)
    parser.add_argument(
        '--after_manifest',
        help='Path to the new PlaSA manifest file, from an SDK release.',
        required=True)
    parser.add_argument(
        '--kinds',
        nargs='+',
        help='Type of PlaSA Fragments to diff.',
        required=True)
    parser.add_argument(
        '--utils_dir',
        help='Path to directory holding all the diffing tools.',
        required=True)
    args = parser.parse_args()

    pd = PlasaDiffer(**vars(args))
    pd.run()


if __name__ == "__main__":
    sys.exit(main())
