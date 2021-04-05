#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Attempts to find unused jiri projects.
For each project in a given jiri manifest file, do the following:
1. Attempt to break incoming deps to this project by renaming the path in which
   it's installed.
2. Try to `fx gen`, as a smoke test for whether this is unused. Log the result.
3. If #2 was successful, upload a change to remove the project and log the URL.

The data is collected in a csv.
Users are encouraged to follow up on all changes uploaded and see if they
passed CQ, then add another column with said result. Changes that didn't pass
CQ probably indicate that the project is used.

Example usage:
$ fx set ...
$ scripts/jiri/find_unused.py \
--jiri_manifest integration/third_party/flower
--csv output.csv
"""

import argparse
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def run_returncode(*command, **kwargs):
    return subprocess.run(
        command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        **kwargs).returncode


def check_output(*command, **kwargs):
    try:
        return subprocess.check_output(
            command, stderr=subprocess.STDOUT, encoding="utf8", **kwargs)
    except subprocess.CalledProcessError as e:
        print("Failed: " + " ".join(command), file=sys.stderr)
        print(e.output, file=sys.stderr)
        raise e


def main():
    parser = argparse.ArgumentParser(
        description="Collects data useful in trimming unused jiri projects")
    parser.add_argument(
        "--jiri_manifest", help="Input jiri manifest file", required=True)
    parser.add_argument(
        "--csv",
        help="Output csv file",
        type=argparse.FileType('w'),
        required=True)
    args = parser.parse_args()

    URL = re.compile("https:\/\/\S*")

    # Check that integration repository is clean
    manifest_dir = os.path.dirname(args.jiri_manifest)

    def check_git_command(*command):
        try:
            check_output("git", *command, cwd=manifest_dir)
        except subprocess.CalledProcessError as e:
            print("Failed: git " + " ".join(command), file=sys.stderr)
            print(e.output, file=sys.stderr)
            raise e

    check_git_command("status", "--porcelain")

    manifest_contents = open(args.jiri_manifest).read()
    # Parse jiri manifest
    manifest = ET.parse(args.jiri_manifest)
    root = manifest.getroot()
    projects = root.find("projects")
    print(f"Found {len(projects)} projects.")

    for project in projects.findall("project"):
        path = project.get("path")
        print(f"Checking {path}...")
        if not os.path.exists(path):
            print(f"{path} not in your checkout!")
            args.csv.write(f"{path}\n")
            continue

        # Break path and see if GN notices
        os.rename(path, path + "_broken_by_find_unused_py")
        gen_result = run_returncode("fx", "gen")
        os.rename(path + "_broken_by_find_unused_py", path)
        if gen_result != 0:
            args.csv.write(f"{path},false\n")
            print(f"{path} is used!")
            continue

        # Remove project and send to CQ
        # We could simply remove the tree element and write to the file, but
        # this will destroy any comments and formatting.
        # Instead, mess around with regular expressions to delete in-place.
        # Perhaps a more robust solution is to first ensure that the jiri
        # manifest is formatted in such a way that we can recreate, such as by
        # parsing it, serializing it to bytes, and comparing it to the original
        # file bytes.
        # This option is left as an exercise to the reader.
        new_manifest_contents = re.sub(
            "^\s*<project [^\>]*?path\s*=\s*\"" + re.escape(path) +
            "\"[\S\s]*?\/>\n",
            "",
            manifest_contents,
            flags=re.MULTILINE)
        open(args.jiri_manifest, 'w').write(new_manifest_contents)
        check_git_command(
            "commit", "-a", "-m", f"[jiri] Check if {path} is unused")
        print(f"Sending {path} removal to CQ... ", end='')
        upload_out = check_output(
            "jiri", "upload", "-l", "Commit-Queue+1", cwd=manifest_dir)
        url = URL.search(upload_out)[0]
        print(url)
        args.csv.write(f"{path},true,{url}\n")

        # Undo project removal
        check_git_command("checkout", "HEAD^")


if __name__ == "__main__":
    sys.exit(main())
