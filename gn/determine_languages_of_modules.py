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
    return languages


def main():
    parser = argparse.ArgumentParser(description="Determine languages used by set of modules")
    parser.add_argument("--modules", help="list of modules", default="packages/gn/default")
    args = parser.parse_args()

    languages = resolve_imports(args.modules.split(","))

    for language in ["cpp", "dart", "go", "rust"]:
        sys.stdout.write("have_%s = %s\n" % (language, str(language in languages).lower()))
    return 0

if __name__ == "__main__":
    sys.exit(main())
