#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''
Looks for types that are never used or are used only once, as a field within
another type.
'''

import sys
from collections import defaultdict

from ir import Libraries, Enum, Struct, DeclState

fidl_libraries = Libraries()

# count uses of named types
type_uses_in_type = defaultdict(lambda: 0)
type_uses_in_argument = defaultdict(lambda: 0)

def count_type(t, d):
    if t.kind == 'identifier':
        d[t['identifier']] += 1
    elif t.kind in ('array', 'vector'):
        count_type(t.element_type, d)
    elif t.kind == 'request':
        d[t['subtype']] += 1

for struct in fidl_libraries.structs:
    for member in struct.members:
        count_type(member.type, type_uses_in_type)

for union in fidl_libraries.unions:
    for member in union.members:
        count_type(member.type, type_uses_in_argument)

# TODO: tables

for method in fidl_libraries.methods:
    for arg in (method.request() or []) + (method.response() or []):
        count_type(arg.type, type_uses_in_argument)

once = []
never = []

# Look for structs that are only used once
for struct in fidl_libraries.structs:
    if type_uses_in_argument[struct.name] == 0:
        if type_uses_in_type[struct.name] == 0:
            never.append(struct.name)
        if type_uses_in_type[struct.name] == 1:
            once.append(struct.name)

# Look for unions that are only used once
for union in fidl_libraries.unions:
    if type_uses_in_argument[union.name] == 0:
        if type_uses_in_type[union.name] == 0:
            never.append(union.name)
        if type_uses_in_type[union.name] == 1:
            once.append(union.name)

once.sort()
never.sort()

print('Never:')
for t in never:
    print('  ' + t)

print('\nOnce:')
for t in once:
    print('  ' + t)
