#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
from pathlib import Path

FUCHSIA_DIR = Path(os.environ["FUCHSIA_DIR"])


def main():
    for path in FUCHSIA_DIR.rglob('*.fidl'):
        if not path.is_file():
            continue
        relpath = str(path.relative_to(FUCHSIA_DIR))
        if relpath.startswith('out/default') or relpath.startswith('prebuilt/'):
            continue
        if relpath.endswith('goodformat.test.fidl'):
            continue
        
        with open(path, 'r') as f:
            lines = f.readlines()
            # already converted from a previous run; skip
            if any(l.startswith('deprecated_syntax;') for l in lines):
                continue

            lib_line = None
            for i, line in enumerate(lines):
                is_comment = line.startswith('//') and not line.startswith('///')
                if not is_comment:
                    lib_line = i
                    break
            if lib_line is None:
                print(path)
            lines.insert(lib_line, 'deprecated_syntax;\n')
        
        with open(path, 'w') as f:
            f.writelines(lines)

if __name__ == '__main__':
    main()
