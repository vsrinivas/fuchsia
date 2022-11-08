#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit test for main.py.

To manually run this test:
  python3 main_test.py
"""
import json
import os
import subprocess
import tempfile
import time
import unittest
import unittest.mock as mock

import main

_TARGET_1 = 'foo.cc.o'
_TARGET_2 = 'baz/bar.cc.o'
_TARGET_3 = 'biz/fuzz.cc.o'
_TARGETS = [_TARGET_1, _TARGET_2, _TARGET_3]


def setup_compdb_and_fs(directory, targets, changed):
  compdb = []
  changed_comp_db = []
  # Setup targets
  for target in targets:
    if not target.endswith('.o'):
      raise Exception('Expected all targets to end with \'.o\', '
                      'but received %s.'%(target))

    # Add the target to the compdb
    target_def = {
        'file': target[:-2],
        'directory': directory,
        'command': 'clang++ -o %s'%(target)
    }
    compdb.append(target_def)

    # If it is expected to change, add the target to the changed compdb
    if target in changed:
      changed_comp_db.append(target_def)

    # Make the placeholder file to set the last modified time before fx ninja
    target_path = os.path.join(directory, target)
    os.makedirs(os.path.dirname(target_path), exist_ok=True)
    with open(target_path, 'w') as f:
      f.write('test placeholder')

  return (compdb, changed_comp_db)


class RunCsaHelperTest(unittest.TestCase):

  def helper(self, temp_dir, targets, changed):
    ninja_path = 'tmp/ninja'
    out_dir = os.path.join(temp_dir, 'foo/bar')

    # Set up the output directory and compdbs
    compdb, expected_changed_compdb = (setup_compdb_and_fs(
        out_dir, targets, changed))

    # Write the compdb
    input_file = os.path.join(temp_dir, 'compile_commands.json')
    with open(input_file, 'w') as in_file:
      json.dump(compdb, in_file)

    # Mock out the xargs fx ninja call
    fake_ninja = FakeNinja(out_dir, changed)
    with mock.patch('subprocess.run') as mock_run:
      mock_run.side_effect = fake_ninja.run

      output_file = os.path.join(temp_dir, 'modified_compile_commands.json')
      args = ['--input', input_file, '--output', output_file,
              '--ninja', ninja_path]

      # Ensure the script succeeded
      self.assertEqual(0, main.main(args))

      # Check that the output compdb contains the files that were modified
      with open(output_file, 'r') as out_file:
        self.assertEqual(
            json.dumps(expected_changed_compdb, indent=2), out_file.read())

  def test_no_changed_files(self):
    with tempfile.TemporaryDirectory() as temp_dir:
      self.helper(temp_dir, _TARGETS, [])

  def test_some_changed_files(self):
    with tempfile.TemporaryDirectory() as temp_dir:
      self.helper(temp_dir, _TARGETS, [_TARGET_1, _TARGET_3])

  def test_all_changed_files(self):
    with tempfile.TemporaryDirectory() as temp_dir:
      self.helper(temp_dir, _TARGETS, [_TARGET_1, _TARGET_2, _TARGET_3])


class FakeNinja(object):

  def __init__(self, directory, files_to_update):
    self.directory = directory
    self.files_to_update = files_to_update
    return

  def run(self, *argv, **kwargs):
    del argv
    del kwargs

    # Sleep for a second so that the modified time check changes
    time.sleep(1)

    for file_to_update in self.files_to_update:
      target_path = os.path.join(self.directory, file_to_update)
      if not os.path.exists(target_path):
        raise Exception('Expected path %s to already exist.'%(target_path))
      with open(target_path, 'w') as f:
        f.write('updated')

    return subprocess.CompletedProcess(args=[], returncode=0, stdout=b'')


if __name__ == '__main__':
  unittest.main()
