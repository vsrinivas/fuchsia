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
        self.json_result = {
            'targets': [],
            'data_deps': [],
        }

    def import_resolved(self, config, config_path):
        self.json_result['targets'] += config.get('packages', {}).values()
        self.json_result['data_deps'] += config.get('labels', [])


def main():
    parser = argparse.ArgumentParser(description='''
Determine labels and Fuchsia packages included in the current build.
''')
    parser.add_argument('--packages',
                        help='JSON list of packages',
                        required=True)
    args = parser.parse_args()

    observer = PackageLabelObserver()
    imports_resolver = PackageImportsResolver(observer)
    imported = imports_resolver.resolve_imports(json.loads(args.packages))

    if imported == None:
        return 1

    json.dump(observer.json_result, sys.stdout, sort_keys=True)

    return 0

if __name__ == "__main__":
    sys.exit(main())
