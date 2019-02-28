#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import absolute_import
from __future__ import print_function

import argparse
import os
import sys
import xml.etree.ElementTree


def main():
    parser = argparse.ArgumentParser(
        description='Identify the vendor repository, if specified, in the Jiri manifest file',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    args = parser.parse_args()

    manifest = os.path.join(os.path.dirname(__file__),
                            os.path.pardir, os.path.pardir, '.jiri_manifest')

    tree = xml.etree.ElementTree.parse(manifest)

    for elt in tree.iter('overrides'):
      for project in elt.findall('project'):
        name = project.attrib.get('name', '')
        if name.startswith('vendor/'):
            print(name)
            return 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
