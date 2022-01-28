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
    left: Fragment
    right: Fragment
    message: str
    is_compatible: bool

    def get_message(self):
        """Small helper method to make the output more useful info for errors.

        Returns:
            A string representing the Fragment key and match error message.
        """
        return '{}\n{}'.format(self.left.dest, self.message)


@dataclass
class PlasaDiffer:
    """Class that actually performs the PlaSA manifest diffing operation.

    Throws an exception if a compatibility issue is detected, or some other
    processing error occurs.
    """
    left_manifest: str
    left_fragments_root: str
    right_manifest: str
    right_fragments_root: str
    kinds: List[str]
    utils_dir: str
    out_dir: str

    def __post_init__(self):
        if not (os.path.exists(self.left_manifest) and
                os.path.exists(self.right_manifest)):
            raise ValueError('manifest paths cannot be empty and must exist.')
        if not (os.path.exists(self.left_fragments_root) and
                os.path.exists(self.right_fragments_root)):
            raise ValueError('fragment paths cannot be empty and must exist.')
        if self.kinds == None or len(self.kinds) == 0:
            raise ValueError('kinds list cannot be None or empty.')
        if not os.path.exists(self.utils_dir):
            raise ValueError('utils_dir path cannot be empty and must exist.')
        if not os.path.exists(self.out_dir):
            raise ValueError('out_dir path cannot be empty and must exist.')

    def load_manifest(self, manifest, fragments_root):
        """Load the PlaSA Manifest JSON file into a dictionary.

        The 'dest' field of each fragment acts as a unique key, and
        allow us to map to the same fragment in other PlaSA manifest files.

        Returns:
            A dict mapping the fragment 'dest' field to the fragment object.
        """
        d = {}
        with open(manifest) as f:
            data = json.load(f)
            for entry in data:
                # We may want to run this script in separate processes to allow
                # parallel verification. Filter out fragment types that we
                # don't care about in this run.
                if entry['kind'] in self.kinds:
                    cts_path = os.path.join(fragments_root, entry['dest'])
                    if not os.path.exists(cts_path):
                        raise ValueError(
                            'Fragment path {} does not exist'.format(cts_path))
                    entry['cts_path'] = cts_path
                    d[entry['dest']] = Fragment(**entry)
        return d

    def diff_fragments(self, left, right):
        """Diff two fragment files against each other.

        Returns:
            A string message containing any useful output of the diff tool.
            A boolean value: True if the two fragment interfaces are compatible.
        """
        if left.kind != right.kind:
            return 'Left fragment kind ({}) does not match Right fragment kind ({}).'.format(
                left.kind, right.kind), False

        if left.kind == 'api_fidl':
            return self._diff_fidl(left, right)
        else:
            return 'Diffing for fragments of type {} is not yet supported.'.format(
                left.kind), False

    def _diff_fidl(self, left, right):
        """Diff two api_fidl fragment files against each other.

        The above method calls this method to diff FIDL fragment files.
        """

        tool = os.path.join(self.utils_dir, 'fidl_api_diff')
        out_file = os.path.join(self.out_dir, os.path.basename(left.cts_path))
        args = [
            tool,
            '--before-file',
            left.cts_path,
            '--after-file',
            right.cts_path,
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

        left_fragments = self.load_manifest(
            self.left_manifest, self.left_fragments_root)
        right_fragments = self.load_manifest(
            self.right_manifest, self.right_fragments_root)
        matches = {}

        # Match fragments from the left and right PlaSA manifest files,
        # and diff them using the provided tools.
        # Delete them from the dictionaries so we can detect discrepancies.
        for key, left in list(left_fragments.items()):
            del left_fragments[key]

            try:
                right = right_fragments[key]
                del right_fragments[key]
                message, is_compatible = self.diff_fragments(left, right)

            except KeyError:
                right = None
                message = "PlaSA fragment missing in right manifest file."

                # TODO(jcecil): Removing elements from the PlaSA is OK in some
                # situations: e.g. if the element has been deprecated.
                # Design a way to set this variable accordingly.
                is_compatible = False

            finally:
                matches[key] = Match(left, right, message, is_compatible)

        # PlaSA elements that are newly introduced in the right manifest
        # are identified and captured here.
        for key, right in list(right_fragments.items()):
            del right_fragments[key]
            left = None
            message = "PlaSA fragment addition in right manifest file."

            # TODO(jcecil): Consider flagging new additions to the PlaSA
            # manifest here, to ensure they have sufficient CTS test coverage.
            is_compatible = True

            matches[key] = Match(left, right, message, is_compatible)

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
        '--left_manifest',
        help='Path to the old PlaSA manifest file, from the CTS release.',
        required=True)
    parser.add_argument(
        '--left_fragments_root',
        help='Path to the root directory for fragment files in the CTS archive.',
        required=True)
    parser.add_argument(
        '--right_manifest',
        help='Path to the new PlaSA manifest file, from an SDK release.',
        required=True)
    parser.add_argument(
        '--right_fragments_root',
        help='Path to the root directory for fragment files in the target SDK.',
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
