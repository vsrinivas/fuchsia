#!/usr/bin/env python
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script drives the execution of the Mojom parser."""

import os
import platform
import subprocess
import sys


# We assume this script is located in the Mojo SDK in
# tools/bindings/pylib/mojom/parse.
THIS_DIR = os.path.abspath(os.path.dirname(__file__))
SDK_ROOT = os.path.abspath(os.path.join(THIS_DIR, os.pardir, os.pardir,
    os.pardir, os.pardir, os.pardir))
PYTHON_SDK_DIR = os.path.abspath(os.path.join(SDK_ROOT, "python"))
sys.path.insert(0, PYTHON_SDK_DIR)
# In order to use mojom_files_mojom we need to make sure the dummy mojo_system
# can be found on the python path.
sys.path.insert(0, os.path.join(PYTHON_SDK_DIR, "dummy_mojo_system"))

from mojom.generate.generated import mojom_files_mojom
from mojom.generate.generated import mojom_types_mojom
from mojo_bindings import serialization

def RunParser(sdk_root, file_names, import_directories=None,
    meta_data_only=False):
  """Runs the mojom parser. Only 64-bit Linux and Mac is supported.

  Args:
    sdk_root: {str} Absolute path to the root of the Mojo SDK. The parser
        binary is expected to be found in
        <sdk_root>/tools/bindings/mojom_tool/bin/<platform>

    file_names {list of str} Paths to mojom files to be parsed. May be either
        absolute or relative to the current working directory.

    import_directories: {list of str} Optional specification of import
        directories where mojom imports should be searched. The elements of the
        list should be absolute paths or paths relative to the current working
        directory.

    meta_data_only: {bool} If True then the flag -meta-data-only
        will be passed to the parser.

  Returns:
    {str} The serialized mojom_files.MojomFileGraph returned by mojom parser,
    or None if the mojom parser returned a non-zero error code.
  """
  system_dirs = {
      ("Linux", "64bit"): "linux64",
      ("Darwin", "64bit"): "mac64",
      }
  system = (platform.system(), platform.architecture()[0])
  if system not in system_dirs:
    raise Exception("The mojom parser only supports Linux or Mac 64 bits.")

  mojom_tool = os.path.join(sdk_root, "tools", "bindings", "mojom_tool",
      "bin", system_dirs[system], "mojom")

  if not os.path.exists(mojom_tool):
    raise Exception(
        "The mojom parser could not be found at %s. "
        "You may need to run gclient sync."
        % mojom_tool)

  cmd = [mojom_tool, "parse"]
  if import_directories:
    cmd.extend(["-I", ",".join(import_directories)])
  if meta_data_only:
    cmd.extend(["-meta-data-only"])

  cmd.extend(file_names)

  try:
    return subprocess.check_output(cmd)
  except subprocess.CalledProcessError:
    return None


def DeserializeMojomFileGraph(serialized_bytes):
  """Deserializes a mojom_files.MojomFileGraph.

  Args:
    serialized_bytes: {str} The serialized mojom_files.MojomFileGraph returned
        by mojom parser
  Returns:
    {mojom_files.MojomFileGraph} The deserialized MojomFileGraph.
  """
  data = bytearray(serialized_bytes)
  context = serialization.RootDeserializationContext(data, [])
  return mojom_files_mojom.MojomFileGraph.Deserialize(context)

def ParseToMojomFileGraph(sdk_root, file_names, import_directories=None,
    meta_data_only=False):
  """Runs the mojom parser and deserializes the result. Only 64-bit Linux and
     Mac is supported.

  Args:
    sdk_root: {str} Absolute path to the root of the Mojo SDK. The parser
        binary is expected to be found in
        <sdk_root>/tools/bindings/mojom_tool/bin/<platform>

    file_names {list of str} Paths to mojom files to be parsed. May be either
        absolute or relative to the current working directory.

    import_directories: {list of str} Optional specification of import
        directories where mojom imports should be searched. The elements of the
        list should be absolute paths or paths relative to the current working
        directory.

    meta_data_only: {bool} If True then the flag -meta-data-only
        will be passed to the parser.

  Returns:
    {mojom_files.MojomFileGraph} The deserialized MojomFileGraph obtained by
    deserializing the bytes returned by mojom parser, or None if the mojom
    parser returned a non-zero error code.
  """
  serialized_bytes = RunParser(sdk_root, file_names, import_directories,
      meta_data_only)
  if serialized_bytes is None:
    return None
  return DeserializeMojomFileGraph(serialized_bytes)
