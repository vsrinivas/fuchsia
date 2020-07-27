#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys


def print_list(name, content):
    quoted = ['"%s"' % c for c in content]
    print('%s = [%s]' % (name, ', '.join(quoted)))


def main():
    deps = sys.argv[1:]

    def is_3p(dep):
        return dep.startswith('//third_party')

    print_list('third_party', list(filter(is_3p, deps)))
    print_list('local', [d for d in deps if not is_3p(d)])
    return 0


if __name__ == '__main__':
    sys.exit(main())
