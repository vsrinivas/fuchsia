"""
Copyright 2017 The Fuchsia Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.

This script is a wrappre for the manifest module that allows users
to build minfs images from system.bootfs.manifest files.

"""

import os
import subprocess
import tempfile

# Try to create the zedboot bootfs that includes the kernel and other data.
# If cmd_line is a file that exists, that will be used as the command line
# bundled into the file. If the argument does not refer to a file, it, itself
# is used as the command line. This calls a subprocess and may raise an
# exception if that process can not be started or the process encounters an
# error.
def make_zedboot(mkbootfs_bin, kernel_path, bootdata_path, cmd_line, out_path):
  remove = False
  if not os.path.exists(cmd_line):
    temp_file, path = tempfile.mkstemp()
    os.write(temp_file, "%s" % cmd_line)
    os.close(temp_file)
    cmd_line = path
    remove = True
    print "Command line written to %s" % cmd_line

  mkbootfs_cmd = [mkbootfs_bin, "-o", out_path, kernel_path, "-C", cmd_line,
                  bootdata_path]
  subprocess.check_call(mkbootfs_cmd)
  if remove:
    os.remove(cmd_line)
