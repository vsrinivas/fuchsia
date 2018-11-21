#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TOOD(TO-908): This script should be replaced with a jiri feature:
# `jiri import -json-output` to yield imports in some JSON schema.
# That could be parsed directly from GN.

from __future__ import absolute_import
from __future__ import print_function

import argparse
import os
import re
import sys
import xml.etree.ElementTree


LAYERS_RE = re.compile('^(garnet|peridot|topaz|vendor/.*)$')


# Returns True iff LAYERS_RE matches name.
def print_if_layer_name(name):
    if LAYERS_RE.match(name):
        print(name)
        return True
    return False


def main():
    parser = argparse.ArgumentParser(
        description='Guess the current cake layer from the Jiri manifest file',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument(
        'manifest', type=argparse.FileType('r'), nargs='?',
        default=os.path.normpath(
            os.path.join(os.path.dirname(__file__),
                         os.path.pardir, os.path.pardir, '.jiri_manifest')))
    args = parser.parse_args()

    tree = xml.etree.ElementTree.parse(args.manifest)

    if tree.find('overrides') is None:
      sys.stderr.write('found no overrides. guessing project from imports\n')
      for elt in tree.iter('import'):
        # For people that haven't switched to the flower model, keep using the
        # old method of guessing the import.
        if print_if_layer_name(elt.attrib['name']):
          return 0
        # For people that have switched to the flower model, but don't use any
        # overrides because they want to be at GI on all layers.
        # manifest can be something like garnet/garnet in which case we want
        # garnet or internal/vendor/foo/bar in which case we want vendor/foo.
        head, name = os.path.split(elt.attrib['manifest'])
        if 'vendor/' in head:
          head, name = os.path.split(head)
          name = os.path.join('vendor', name)
        if print_if_layer_name(name):
          return 0

    # Guess the layer from the name of the <project> that is overriden in the
    # current manifest.
    for elt in tree.iter('overrides'):
      for project in elt.findall('project'):
        if print_if_layer_name(project.attrib.get('name', '')):
          return 0

    sys.stderr.write("ERROR: Could not guess petal from %s. "
                     "Ensure 'boards' and either 'products' or 'packages' is set.\n"
                     % args.manifest.name)

    return 2


if __name__ == '__main__':
    sys.exit(main())
