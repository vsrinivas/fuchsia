#!/usr/bin/env python3.8
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
generate fidl_project.json file declaring FIDL libraries

The first command line argument is the root $FUCHSIA_DIR.
The second command line argument is the path to generated_sources.json.
The third command line argument is the path to which the script will write
fidl_project.json.

This script reads the generated_sources.json file which contains the paths to
fidlc-generated JSON IR, and generates a fidl_project.json file which declares
all FIDL libraries along with their constituent files, dependencies, and build
artifacts (JSON IR and bindings). This is for use in the FIDL Language Server,
which uses fidl_project to do dependency resolution.
"""

import glob
import json
import os
import re
import sys
from pathlib import Path

# fidl_project.json schema: list of Library
# where Library is
# {
#     "name": string,
#     "files": []string,
#     "json": string,
#     "deps": []string,
#     "bindings": {
#         "hlcpp": {},
#         "llcpp": {},
#         "rust": {},
#         "go": {},
#         "dart": {},
#         ...
#     }
# }

# https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/language.md#identifiers
identifier_pattern = r'[a-zA-Z](?:[a-zA-Z0-9_]*[a-zA-Z0-9])?'

# Although "library" can be used anywhere (e.g. as a type name), this regex
# is robust because the the library declaration must appear at the top of
# the file (only comments and whitespace can precede it).
library_pattern = (
    r'^(?:\s*//[^\n]*\n)*\s*' +
    r'library\s+(' +
    identifier_pattern +
    r'(?:\.' + identifier_pattern + r')*' +
    r')\s*;'
)

def find_files(library_name, library_json, fuchsia_dir=""):
    pattern = r'^fidling\/gen\/([\w\.\/-]+)\/[\w\-. ]+\.fidl\.json$'
    result = re.search(pattern, library_json)
    if not result or not result.group(1):
        return []

    fidl_dir = Path(f'{fuchsia_dir}/{result.group(1)}')
    globs = [
        fidl_dir.glob('*.fidl'),
        fidl_dir.parent.glob('*.fidl'),
    ]

    files = []
    for glob in globs:
        for file in glob:
            # Read in FIDL file
            with open(file, 'r') as f:
                # Parse `library` decl
                result = re.search(library_pattern, f.read())
                # Check that it matches library name
                if not result or not result.group(1):
                    continue
                if result.group(1) != library_name:
                    continue
            files.append(str(file))
    return files


def find_deps(library_json, fuchsia_dir=""):
    library_json_path = Path(f'{fuchsia_dir}/out/default/{library_json}')

    if not os.path.isfile(library_json_path):
        return None

    with open(library_json_path, 'r') as f:
        library = json.load(f)
        deps = library['library_dependencies']
        deps = [dep['name'] for dep in deps]
        return deps


def gen_fidl_project(fuchsia_dir="", generated_sources_path="", fidl_project_path=""):
    result = []
    with open(generated_sources_path, 'r') as f:
        artifacts = json.load(f)

    processed = set()
    for artifact in artifacts:
        if artifact in processed:
            continue

        if not artifact.endswith('.fidl.json'):
            continue

        deps = find_deps(artifact, fuchsia_dir=fuchsia_dir)
        if deps is None:
            continue

        # Get JSON filename out of artifact
        library_name = os.path.basename(artifact)
        # Remove .fidl.json suffix
        library_name = library_name[:-10]

        processed.add(artifact)
        result.append({
            'name': library_name,
            'json': f"{fuchsia_dir}/out/default/{artifact}",
            'files': find_files(library_name, artifact, fuchsia_dir=fuchsia_dir),
            'deps': deps,
            'bindings': {},  # TODO
        })

    with open(fidl_project_path, 'w') as f:
        json.dump(result, f, indent=4, sort_keys=True)


if __name__ == '__main__':
    if len(sys.argv) < 4:
        print('Please run this script as:')
        print('  fx exec scripts/generate-fidl-project.py <root/build/dir> <generated_sources.json> <fidl_project.json>')
        sys.exit(1)

    gen_fidl_project(
        fuchsia_dir=sys.argv[1],
        generated_sources_path=sys.argv[2],
        fidl_project_path=sys.argv[3],
    )
