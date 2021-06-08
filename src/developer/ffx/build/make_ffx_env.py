#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(description='Make empty ffx environment')
    parser.add_argument("env_file", type=argparse.FileType('w'))
    parser.add_argument("build_config", type=argparse.FileType('w'))
    args = parser.parse_args()

    ffx_env = {
        "user": "",
        "build": "",
        "global": args.build_config.name,
    }

    ffx_build_config = {"ffx": {"analytics": {"disabled": True}}}

    json.dump(ffx_build_config, args.build_config, indent=2)
    json.dump(ffx_env, args.env_file, indent=2)


if __name__ == '__main__':
    sys.exit(main())
