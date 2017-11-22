#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os.path
import paths
import sys

def resolve_imports(import_queue):
    # Hack: Add cpp manifest until we derive runtime information from the
    # package configs themselves.
    import_queue.append('packages/gn/cpp')
    imported = set(import_queue)
    languages = set()
    while import_queue:
        config_name = import_queue.pop()
        config_path = os.path.join(paths.FUCHSIA_ROOT, config_name)
        try:
            with open(config_path) as f:
                try:
                    config = json.load(f)
                    if config.get("languages"):
                        languages.update(config.get("languages"))
                    for i in config.get("imports", []):
                        if i not in imported:
                            import_queue.append(i)
                            imported.add(i)
                except Exception as e:
                    import traceback
                    traceback.print_exc()
                    sys.stderr.write("Failed to parse config %s, error %s\n" % (config_path, str(e)))
                    return None
        except IOError, e:
            sys.stderr.write("Failed to read package '%s' from '%s'.\n" % (config_name, config_path))
            if "/" not in config_name:
                sys.stderr.write("""
Package names are relative to the root of the source tree but the requested path
did not contain a '/'. Did you mean 'packages/gn/%s' instead?
""" % config_name)
            return None
    return languages, imported


def main():
    parser = argparse.ArgumentParser(description="Determine languages used by set of modules")
    parser.add_argument("--modules", help="list of modules", default="packages/gn/default")
    args = parser.parse_args()

    languages, imported = resolve_imports(args.modules.split(","))
    if languages is None:
        return -1

    for language in ["cpp", "dart", "go", "rust"]:
        sys.stdout.write("have_%s = %s\n" % (language, str(language in languages).lower()))
    sys.stdout.write("imported = [%s]\n" % ",".join(map(lambda package: '"//%s"' % package, imported)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
