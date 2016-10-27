#!/usr/bin/env python
# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates the list of dart source file outputs from a mojom.Module."""

import argparse
import os
import re
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SDK_DIR = os.path.join(SCRIPT_DIR, os.path.pardir, os.path.pardir)
sys.path.insert(0, os.path.join(SCRIPT_DIR, 'pylib'))

from mojom.error import Error
from mojom.parse import parser_runner
from mojom.generate import mojom_translator

def mojom_path(name, namespace, attributes):
  package_name = 'mojom'
  if attributes and attributes.get('DartPackage'):
    package_name = attributes['DartPackage']
  elements = [package_name, 'lib']
  elements.extend(namespace.split('.'))
  elements.append("%s.dart" % name)
  return os.path.join(*elements)


def process_mojom(path_to_mojom):
  filename = os.path.abspath(path_to_mojom)

  # Parse
  mojom_file_graph = parser_runner.ParseToMojomFileGraph(SDK_DIR, [filename],
      meta_data_only=True)
  if mojom_file_graph is None:
    print("Error parsing %s" % filename)
  mojom_dict = mojom_translator.TranslateFileGraph(mojom_file_graph)
  mojom = mojom_dict[filename]

  # Output path
  attributes = mojom.attributes
  print(mojom_path(mojom.name, mojom.namespace, attributes))


def main():
  parser = argparse.ArgumentParser(description='Output list of ')
  parser.add_argument('--mojoms',
                      metavar='mojoms',
                      nargs='+',
                      required=True)
  args = parser.parse_args()

  for mojom in args.mojoms:
    process_mojom(mojom)

  return 0


if __name__ == '__main__':
    sys.exit(main())