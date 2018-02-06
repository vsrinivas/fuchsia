#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

from package_imports_resolver import PackageImportsResolver

class PackageLangageObserver:
    def __init__(self):
        self.languages = set()
        self.labels = []

    def import_resolved(self, config, config_path):
        for label in config.get("labels", []):
            self.labels.append(label)
        if config.get("languages"):
            self.languages.update(config.get("languages"))


def get_dep_from_package_name(package_name):
    if package_name[0] == '/':
        return '"%s"' % package_name
    return '"//%s"' % package_name

def main():
    parser = argparse.ArgumentParser(description="Determine languages used by a"
                                     + " given set of packages")
    parser.add_argument("--packages", help="list of packages", required=True)
    args = parser.parse_args()


    language_observer = PackageLangageObserver()
    imports_resolver = PackageImportsResolver(language_observer)
    imported = imports_resolver.resolve_imports(args.packages.split(","))
    languages = language_observer.languages
    labels = language_observer.labels

    # Some build tools depend on Go, so we always need to include it.
    languages.add("go")

    for language in ["cpp", "dart", "go", "rust"]:
        sys.stdout.write("have_%s = %s\n" %
                         (language, str(language in languages).lower()))
    sys.stdout.write("imported = [%s]\n" %
                     ",".join(map(get_dep_from_package_name, imported)))
    sys.stdout.write("labels = [%s]\n" %
                     ",".join(['"%s"' % label for label in labels]))
    return 0

if __name__ == "__main__":
    sys.exit(main())
