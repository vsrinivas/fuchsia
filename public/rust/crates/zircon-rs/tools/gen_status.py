#!/usr/bin/env python

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A tool for autogenerating the mapping between Status and mx_status_t
# Usage: python gen_status.py {sys,enum,match} magenta/system/public/magenta/fuchsia-types.def

import re
import sys

status_re = re.compile('FUCHSIA_ERROR\((\w+),\s+(\d+)\)$')

def parse(in_filename):
    result = []
    for line in file(in_filename):
        m = status_re.match(line)
        if m:
            result.append((m.group(1), int(m.group(2))))
    return result

def to_snake_case(name):
    result = []
    for element in name.split('_'):
        result.append(element[0] + element[1:].lower())
    return ''.join(result)

def out(style, l):
    print('// Auto-generated using tools/gen_status.py')
    longest = max(len(name) for (name, num) in l)
    if style == 'sys':
        print('pub const %s : mx_status_t = 0;' % ('NO_ERR'.ljust(longest + 4)))
        for (name, num) in l:
            print('pub const ERR_%s : mx_status_t = %d;' % (name.ljust(longest), -num))
    if style == 'enum':
        print('pub enum Status {')
        print('    NoErr = 0,')
        for (name, num) in l:
            print('    %s = %d,' % (to_snake_case(name), -num))
        print('');
        print('    /// Any mx_status_t not in the set above will map to the following:')
        print('    UnknownOther = -32768,')
        print('}')
    if style == 'match':
        print('            sys::NO_ERROR => Status::NoErr,')
        for (name, num) in l:
            print('            sys::ERR_%s => Status::%s,' % (name, to_snake_case(name)))
        print('            _ => Status::UnknownOther,')


l = parse(sys.argv[2])
out(sys.argv[1], l)
