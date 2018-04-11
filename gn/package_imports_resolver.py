#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os.path
import sys

import paths

class PackageImportsResolver:
    """Recursively resolves imports in build packages. See
       https://fuchsia.googlesource.com/docs/+/master/build_packages.md
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
