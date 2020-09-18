#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

from lib.factory import Factory


def main():
    """Main entry point for "fx fuzz"."""
    factory = Factory()
    args = factory.parser.parse_args()
    factory.host.tracing = args.verbose
    args.command(args, factory)


if __name__ == '__main__':
    sys.exit(main())
