#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import argparse
import os
import sys
import json
import subprocess
import tempfile
from dataclasses import dataclass
from typing import List


@dataclass
class PlasaDiffer:
    fidl_api_diff_path: str

    def find_breaking_changes_in_fragment_file(self, before, after):
        """Diff two api_fidl fragment files against each other.
        The above method calls this method to diff FIDL fragment files.
        """
        diff_file = tempfile.NamedTemporaryFile()
        diff_file.close()
        args = [
            self.fidl_api_diff_path,
            '--before-file',
            before,
            '--after-file',
            after,
            '--api-diff-file',
            diff_file.name,
        ]
        p = subprocess.run(args, check=True, capture_output=True, text=True)
        diff = []
        with open(diff_file.name, 'r') as f:
            output = json.loads(f.read())
            if output:
                diff = output["api_diff"]
        return [
            self._explain_report_item(item)
            for item in diff
            if item["conclusion"] != "Compatible"
        ]

    def _explain_report_item(self, report_item):
        if not "after" in report_item:
            return "Deleted: {}".format(report_item["before"])
        if not "before" in report_item:
            return "Added: {}".format(report_item["after"])
        return "Changed: {} => {}".format(
            report_item["before"], report_item["after"])


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
    args = parser.parse_args()
    pd = PlasaDiffer(**vars(args))


if __name__ == "__main__":
    sys.exit(main())
