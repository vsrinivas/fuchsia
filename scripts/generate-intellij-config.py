#!/usr/bin/env python2.7

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from lxml import etree
import os
import re
import subprocess
import sys

try:
  fuchsia_dir = os.environ['FUCHSIA_DIR']
except KeyError, e:
  print 'Missing FUCHSIA_DIR environment variable. Please run this script as:'
  print '  fx exec scripts/generate-intellij-config.py'
  sys.exit(1)

idea_dir = os.path.join(fuchsia_dir, '.idea')


def find_dart_directories():
  # Ask GN for all labels that depends on the target Dart SDK.
  refs = subprocess.check_output([
      os.path.join(fuchsia_dir, 'buildtools',
                   'gn'), 'refs', os.environ['FUCHSIA_BUILD_DIR'],
      '//dart:create_sdk(//build/toolchain:host_x64)'
  ])
  # Turn that into a set of unique directories for the labels.
  label_dirs = {
      os.path.join(fuchsia_dir, re.sub(r':.*', '', label[2:]))
      for label in refs.split('\n') if len(label)
  }
  # Filter to just the ones that contain dart code.
  dart_dirs = [
      d for d in label_dirs if os.path.exists(os.path.join(d, 'pubspec.yaml'))
  ]
  # Sort them by the leaf name. This how IntelliJ seems to sort them.
  dart_dirs.sort(key=lambda d: os.path.basename(d))

  return dart_dirs


def write_dart_iml(dart_dir):
  # TODO(ianloic): check if it already exists?
  # TODO(ianloic): handle tests specially
  relative_dart_dir = os.path.relpath(dart_dir, fuchsia_dir)
  iml_file = os.path.join(idea_dir, 'modules', relative_dart_dir) + '.iml'
  iml_dir = os.path.dirname(iml_file)
  if not os.path.exists(iml_dir):
    os.makedirs(iml_dir)
  relative_source_dir = os.path.relpath(dart_dir, iml_dir)
  module = etree.Element('module', type='WEB_MODULE', version='4')
  component = etree.SubElement(module, 'component', name='NewModuleRootManager')
  component.set('inherit-compiler-output', 'true')
  etree.SubElement(component, 'exclude-output')
  etree.SubElement(
      component, 'content', url='file://$MODULE_DIR$/' + relative_source_dir)
  etree.SubElement(component, 'orderEntry', type='inheritedJdk')
  etree.SubElement(
      component, 'orderEntry', type='sourceFolder', forTests='false')

  with open(iml_file, 'w') as f:
    f.write(
        etree.tostring(
            module, encoding='UTF-8', xml_declaration=True, pretty_print=True))

  return iml_file


def write_dart_modules(dart_dirs):
  # TODO(ianloic): merge with existing file?
  project = etree.Element('project', version='4')
  component = etree.SubElement(
      project, 'component', name='ProjectModuleManager')
  modules = etree.SubElement(component, 'modules')
  for dart_dir in dart_dirs:
    iml_file = write_dart_iml(dart_dir)
    relative_dart_dir = os.path.relpath(dart_dir, fuchsia_dir)
    relative_iml_file = os.path.relpath(iml_file, fuchsia_dir)
    etree.SubElement(
        modules,
        'module',
        fileurl='file://$PROJECT_DIR$/' + relative_iml_file,
        filepath='$PROJECT_DIR$/' + relative_iml_file,
        group=os.path.dirname(relative_dart_dir))

  with open(os.path.join(idea_dir, 'modules.xml'), 'w') as f:
    f.write(
        etree.tostring(
            project, encoding='UTF-8', xml_declaration=True, pretty_print=True))


def write_basic_project_files():
  # Make the .idea directory if needed.
  if not os.path.exists(idea_dir):
    os.makedirs(idea_dir)

  # Create skeleton misc.xml and workspace.xml.
  skeleton = etree.tostring(
      etree.Element('project', version='4'),
      encoding='UTF-8',
      xml_declaration=True,
      pretty_print=True)
  for fn in ('misc.xml', 'workspace.xml'):
    path = os.path.join(idea_dir, fn)
    if not os.path.exists(path):
      with open(path, 'w') as f:
        f.write(skeleton)


if __name__ == '__main__':
  dart_dirs = find_dart_directories()
  write_basic_project_files()
  write_dart_modules(dart_dirs)
