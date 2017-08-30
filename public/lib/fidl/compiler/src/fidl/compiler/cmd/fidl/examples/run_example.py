#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script runs the example_consumer on the example.mojom file. To invoke it
simply run it to see the output of the example_consumer.

This script assumes that its location is
<src_root>/mojom/mojom_tool/examples/run_example.py
This script also assumes that the mojom parser can be found at
<src_root>/mojo/public/tools/bindings/mojom_tool/bin/<os_arch>/mojom_tool
"""
import os
import platform
import subprocess
import sys


def ParseSampleFile():
  """Parses the example mojom file using the mojom parser and returns the
  serialized file graph that is produced.

  Returns:
    A string representation of the serialized file graph if succesful, None
    otherwise.
  """
  system_dirs = {
      ('Linux', '64bit'): 'linux64',
      ('Darwin', '64bit'): 'mac64',
      }
  system = (platform.system(), platform.architecture()[0])
  if system not in system_dirs:
    raise Exception('The mojom parser only supports Linux or Mac 64 bits.')

  examples_dir = os.path.dirname(os.path.abspath(__file__))
  src_root = os.path.join(examples_dir, '../../..')
  mojom_tool = os.path.join(src_root, 'mojo', 'public', 'tools', 'bindings',
      'mojom_tool', 'bin', system_dirs[system], 'mojom_tool')
  if not os.path.exists(mojom_tool):
    raise Exception(
        'The mojom parser could not be found at %s. '
        'You may need to run gclient sync.'
        % mojom_tool)

  examples_mojom = os.path.join(examples_dir, 'example.mojom')
  cmd = [mojom_tool, examples_mojom]

  try:
    return subprocess.check_output(cmd)
  except subprocess.CalledProcessError:
    return None


def RunExampleConsumer(serialized_file_graph):
  """Runs the example consumer on the serialized_file_graph.

  Args:
    serialized_file_graph: mojom_files.MojomFileGraph as output by the mojom
      parser.

  Returns:
    The integer exit code of the example consumer.
  """
  examples_dir = os.path.dirname(os.path.abspath(__file__))
  example_consumer = os.path.join(examples_dir, 'example_consumer.go')
  src_root = os.path.abspath(os.path.join(examples_dir, '../../..'))

  environ = { 'GOPATH': os.path.dirname(src_root) }
  print environ
  cmd = ['go', 'run', example_consumer]
  process = subprocess.Popen(cmd, stdin=subprocess.PIPE, env=environ)
  process.communicate(serialized_file_graph)
  return process.wait()


def main():
  serialized_file_graph = ParseSampleFile()

  if serialized_file_graph:
    return RunExampleConsumer(serialized_file_graph)
  return 1

if __name__ == '__main__':
  sys.exit(main())
