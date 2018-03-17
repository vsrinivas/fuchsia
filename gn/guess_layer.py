#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TOOD(TO-908): This script should be replaced with a jiri feature:
# `jiri import -json-output` to yield imports in some JSON schema.
# That could be parsed directly from GN.

import argparse
import os
import re
import sys
import xml.etree.ElementTree


LAYERS_RE = re.compile('^(garnet|peridot|topaz|vendor/.*)$')


# Returns 0 if name does not match LAYERS_RE.
# Returns 1 and prints name if it does match.
def check_import(name):
    if LAYERS_RE.match(name):
        print name
        return 1
    return 0


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

    if sum(check_import(elt.attrib['name']) for elt in tree.iter('import')):
        return 0

    sys.stderr.write('ERROR: Could not guess cake layer from %s\n' %
                     args.manifest.name)
    return 2


if __name__ == '__main__':
    sys.exit(main())
