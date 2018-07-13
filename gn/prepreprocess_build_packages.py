#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os.path
import paths
import sys


class PackageImportsResolver:
    """Recursively resolves imports in build packages. See
       https://fuchsia.googlesource.com/docs/+/master/development/build/packages.md
       for more information about build packages.

       An observer may be used to perform additional work whenever an
       import is resolved. This observer needs to implement a method with this
       signature:

       def import_resolved(self, config, config_path)

       where config is the JSON file representing the build package.

       If there was an error reading some of the input files, `None` will be
       returned.
       """

    def __init__(self, observer=None):
        self.observer = observer

    def resolve(self, imports):
        return self.resolve_imports(imports)

    def resolve_imports(self, import_queue):

        def detect_duplicate_keys(pairs):
            keys = set()
            result = {}
            for k, v in pairs:
                if k in keys:
                    raise Exception("Duplicate key %s" % k)
                keys.add(k)
                result[k] = v
            return result

        imported = set(import_queue)
        while import_queue:
            config_name = import_queue.pop()
            config_path = os.path.join(paths.FUCHSIA_ROOT, config_name)
            try:
                with open(config_path) as f:
                    try:
                        config = json.load(f,
                            object_pairs_hook=detect_duplicate_keys)
                        self.observer.import_resolved(config, config_path)
                        for i in config.get("imports", []):
                            if i not in imported:
                                import_queue.append(i)
                                imported.add(i)
                    except Exception as e:
                        import traceback
                        traceback.print_exc()
                        sys.stderr.write(
                            "Failed to parse config %s, error %s\n" %
                            (config_path, str(e)))
                        return None
            except IOError, e:
                sys.stderr.write("Failed to read package '%s' from '%s'.\n" %
                                 (config_name, config_path))
                if "/" not in config_name:
                    sys.stderr.write("""
Package names are relative to the root of the source tree but the requested
path did not contain a '/'. Did you mean 'build/gn/%s' instead?
    """ % config_name)
                return None
        return imported


class PackageLabelObserver:
    def __init__(self):
        self.json_result = {
            'targets': [],
            'data_deps': [],
            'host_tests': [],
            'files_read': [],
        }

    def import_resolved(self, config, config_path):
        self.json_result['targets'] += config.get('packages', [])
        self.json_result['data_deps'] += config.get('labels', [])
        self.json_result['host_tests'] += config.get('host_tests', [])
        self.json_result['files_read'].append(config_path)


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
