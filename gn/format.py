#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import sys
import os
import paths

def main():
    for config in sys.argv[1:]:
        config_path = os.path.join(paths.SCRIPT_DIR, config)
        config_json = {}
        with open(config_path) as config_file:
            config_json = json.load(config_file)
        with open(config_path, 'w') as config_file:
            json.dump(config_json, config_file,
                      indent=4, separators=(',', ': '))
            config_file.write('\n')


if __name__ == '__main__':
    sys.exit(main())
