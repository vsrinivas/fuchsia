#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''
Find protocols with [Layout = "Simple"] that pass unions.
'''

from ir import Libraries, Enum, Struct, Union

fidl_libraries = Libraries()


def is_simple(p):
  attrs = p.get('maybe_attributes', [])
  return any(a['name'] == 'Layout' and a['value'] == 'Simple' for a in attrs)


def has_union(t):
  if t.kind == 'identifier':
    declaration = t.library.libraries.find(t['identifier'])
    if isinstance(declaration, Union):
      return True
    if isinstance(declaration, Struct):
      return any(has_union(m.type) for m in declaration.members)
    # Note: there will be no xunions or tables
    return False
  elif 'element_type' in t:
    return has_union(t.element_type)
  return False


for library in fidl_libraries:
  for protocol in library.interfaces:
    if not is_simple(protocol):
      continue
    for method in protocol.methods:
      args = []
      args.extend(method.request() or [])
      args.extend(method.response() or [])
      if any(has_union(a.type) for a in args):
        print('{}.{} has unions'.format(protocol['name'], method['name']))
