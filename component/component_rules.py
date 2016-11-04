#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys
import urlparse

sys.path.insert(0, os.path.abspath(os.path.join(__file__, '../../../packages/gn')))
from component_manifest import ComponentManifest


def gn(data, indent=''):
    if isinstance(data, basestring):
        assert '\n' not in data
        assert '"' not in data
        return '"%s"' % data
    if isinstance(data, list):
        return '[' + (',\n'+indent).join([gn(i, indent+'  ') for i in data]) + ']'
    if isinstance(data, dict):
        serialized = ''.join(['%s%s = %s\n' % (indent, name, gn(value, indent+'  ')) for name, value in data.items()])
        if indent:
            return '{\n%s%s\n}' % (serialized, indent)
        else:
            return serialized
    raise Exception('unhandled %r' % data)


class ComponentRules(object):
    def __init__(self, manifest_path):
        self.manifest = ComponentManifest(manifest_path)

    def _dict_from_component_file(self, component_file):
        return {
            'src_path': component_file.src_path,
            'url': component_file.url,
            'url_as_path': component_file.url_as_path,
        }

    def print_scope(self):
        files = self.manifest.files()
        scope = {
            'program': self._dict_from_component_file(files[self.manifest.program_resource()]),
            'manifest': self._dict_from_component_file(files['MANIFEST']),
            'files': [
                self._dict_from_component_file(component_file)
                for component_file in self.manifest.files().values()
            ],
        }
        print gn(scope)


if __name__ == '__main__':
    _, manifest_path = sys.argv
    c = ComponentRules(manifest_path)
    c.print_scope()
