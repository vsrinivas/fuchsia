#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os.path
import paths
import sys
from prepreprocess_build_packages import PackageImportsResolver, PackageLabelObserver


def preprocess_packages(packages):
    observer = PackageLabelObserver()
    imports_resolver = PackageImportsResolver(observer)
    imported = imports_resolver.resolve_imports(packages)

    if imports_resolver.errored():
        raise ImportError

    if imported == None:
        return None

    return observer.json_result


def main():
    parser = argparse.ArgumentParser(description="""
Merge a list of product definitions to unique lists of GN labels:

monolith   - the list of packages included in the base system images
preinstall - the list of packages preinstall, but not part of OTA
available  - the list of packages installable and updatable
files_read - a list of files used to compute all of the above
""")
    parser.add_argument("--monolith",
                        help="List of package definitions for the monolith",
                        required=True)
    parser.add_argument("--preinstall",
                        help="List of package definitions for preinstalled packages",
                        required=True)
    parser.add_argument("--available",
                        help="List of package definitions for available packages",
                        required=True)
    args = parser.parse_args()

    build_packages = {
        "monolith": set(),
        "preinstall": set(),
        "available": set(),
        "files_read": set(),
    }


    # Parse monolith, preinstall, and available sets.
    build_packages["monolith"].update(json.loads(args.monolith))
    build_packages["preinstall"].update(json.loads(args.preinstall))
    build_packages["available"].update(json.loads(args.available))

    try:
        monolith_results = preprocess_packages(list(build_packages["monolith"]))
        preinstall_results = preprocess_packages(list(build_packages["preinstall"]))
        available_results = preprocess_packages(list(build_packages["available"]))
    except ImportError:
        return 1

    for res in (monolith_results, preinstall_results, available_results):
        if res is None:
            continue
        if res["files_read"]:
            build_packages["files_read"].update(res["files_read"])

    monolith_targets = set(monolith_results["targets"] if monolith_results else ())
    preinstall_targets = set(preinstall_results["targets"] if preinstall_results else ())
    available_targets = set(available_results["targets"] if available_results else ())

    # preinstall_targets must not include monolith targets
    preinstall_targets -= monolith_targets

    # available_targets must include monolith and preinstall targets
    available_targets |= monolith_targets | preinstall_targets

    print(json.dumps({
        "monolith": list(monolith_targets),
        "preinstall": list(preinstall_targets),
        "available": list(available_targets),
        "files_read": list(build_packages["files_read"]),
    }))

    return 0

if __name__ == "__main__":
    sys.exit(main())
