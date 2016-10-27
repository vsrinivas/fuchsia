#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# mojom_tool.py invokes the mojom tool built for the operating system and
# architecture on which this script is run. It forwards all of its command
# line arguments to the mojom tool blindly. This script's exit code is the
# mojom tool's exit code.

import os
import platform
import subprocess
import sys

def main(args):
  # We assume this script is located in the Mojo SDK in tools/bindings.
  this_dir = os.path.abspath(os.path.dirname(__file__))

  system_dirs = {
      ('Linux', '64bit'): 'linux64',
      ('Darwin', '64bit'): 'mac64',
      }
  system = (platform.system(), platform.architecture()[0])
  if system not in system_dirs:
    raise Exception('The mojom tool only supports Linux or Mac 64 bits.')

  mojom_tool = os.path.join(
      this_dir, 'mojom_tool', 'bin', system_dirs[system], 'mojom')

  if not os.path.exists(mojom_tool):
    raise Exception(
        "The mojom tool could not be found at %s. "
        "You may need to run gclient sync."
        % mojom_tool)

  cmd = [mojom_tool]
  cmd.extend(args)

  process = subprocess.Popen(cmd)
  return process.wait()

if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
