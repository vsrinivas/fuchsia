#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generates a DOT file representing the hierarchy of build modules.

import json
import os
import paths
import sys


def main():
    packages_directory = os.path.join(paths.FUCHSIA_ROOT, 'packages', 'gn')
    print('digraph fuchsia {')
    for directory, _, files in os.walk(packages_directory):
        modules = [file for file in files
                   if file.find('.') == -1 and file != 'default']
        sanitize = lambda name: name.replace('-', '_')
        for module in modules:
            with open(os.path.join(directory, module)) as content:
                data = json.load(content)
                if 'imports' not in data:
                    continue
                id = sanitize(module)
                print('%s [label="%s"];' % (id, module))
                for dep in data['imports']:
                    print('%s -> %s;' % (id, sanitize(dep)))
        break
    print('}')


if __name__ == "__main__":
    sys.exit(main())
