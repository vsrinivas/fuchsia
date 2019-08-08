#!/usr/bin/env python2.7

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import itertools
import os
import subprocess
import sys
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(
        description="Find the first integration revision that integrated"
        "a given petal revision.")
    parser.add_argument("petal_path")
    parser.add_argument("petal_revision")
    args = parser.parse_args()

    fuchsia_dir = os.environ["FUCHSIA_DIR"]

    imports = ET.parse(os.path.join(
        fuchsia_dir,
        ".jiri_manifest")).findall('./imports/import[@name=\'integration\']')

    components = []
    for n in imports:
        if n.attrib["remote"].startswith("sso://"):
            components.append("fuchsia")
            break

    components.extend([args.petal_path, "minimal"])

    integration_dir = os.path.join(fuchsia_dir, "integration")
    petal_dir = os.path.join(fuchsia_dir, args.petal_path)

    manifest_path = os.path.join(*components)
    project_xpath = './projects/project[@name=\'%s\']' % args.petal_path

    # Used to avoid shelling out to git multiple times with same petal revision.
    last_petal_revision_checked = None
    descendant_found = False
    for i in itertools.count(start=0, step=1):
        manifest_string = subprocess.check_output([
            "git",
            "-C",
            integration_dir,
            "show",
            "HEAD~%d:%s" % (i, manifest_path),
        ])
        for project in ET.fromstring(manifest_string).findall(project_xpath):
            petal_revision_at_integration_revision = project.attrib["revision"]

        if petal_revision_at_integration_revision != last_petal_revision_checked:
            last_petal_revision_checked = petal_revision_at_integration_revision
            if not subprocess.call([
                    "git",
                    "-C",
                    petal_dir,
                    "merge-base",
                    "--is-ancestor",
                    args.petal_revision,
                    petal_revision_at_integration_revision,
            ]):
                descendant_found = True
            elif descendant_found:
                subprocess.check_call(
                    [
                        "git",
                        "-C",
                        integration_dir,
                        "rev-parse",
                        "HEAD~%d" % (i - 1),
                    ],
                    stdout=sys.stdout,
                    stderr=sys.stderr,
                )
                break


if __name__ == "__main__":
    main()
