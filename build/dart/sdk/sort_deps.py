#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys


def print_list(name, content):
    quoted = map(lambda c: '"%s"' % c, content)
    print('%s = [%s]' % (name, ', '.join(quoted)))


def main():
    deps = sys.argv[1:]

    def is_3p(dep):
        return dep.startswith('//third_party')

    print_list('third_party', filter(is_3p, deps))
    print_list('local', filter(lambda d: not is_3p(d), deps))
    return 0


if __name__ == '__main__':
    sys.exit(main())
