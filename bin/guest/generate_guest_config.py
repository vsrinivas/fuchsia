#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmdline', help='Kernel cmdline string')
    parser.add_argument('--block', action='append', help='Block device spec')
    return parser.parse_args()

def main():
    args = parse_args()
    config = {}
    if (args.cmdline):
        config['cmdline'] = args.cmdline
    if (args.block):
        config['block'] = args.block
    print json.dumps(config, indent=4, separators=(',', ': '))


if __name__ == "__main__":
    main()
