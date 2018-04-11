#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys

from package_imports_resolver import PackageImportsResolver

class PackageLabelObserver:
    def __init__(self):
        self.labels = []

    def import_resolved(self, config, config_path):
        for label in config.get("labels", []):
            self.labels.append(label)


def get_dep_from_package_name(package_name):
    if package_name[0] == '/':
        return '"%s"' % package_name
    return '"//%s"' % package_name

def main():
    parser = argparse.ArgumentParser(description="Determine labels and Fuchsia "
                                     "packages included in the current build")
    parser.add_argument("--packages", help="list of packages", required=True)
    args = parser.parse_args()

    observer = PackageLabelObserver()
    imports_resolver = PackageImportsResolver(observer)
    imported = imports_resolver.resolve_imports(json.loads(args.packages))
    labels = observer.labels

    if imported == None:
        return 1

    sys.stdout.write("imported = [%s]\n" %
                     ",".join(map(get_dep_from_package_name, imported)))
    sys.stdout.write("labels = [%s]\n" %
                     ",".join(['"%s"' % label for label in labels]))
    return 0

if __name__ == "__main__":
    sys.exit(main())
