#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import argparse
import subprocess
import sys

# This script is concerned with creating a new user.bootfs which contains
# whatever the contents of the user.bootfs are according to the supplied
# file_manifest argument PLUS an additional file which is a compressed disk
# image containing the contents of file_manifest. Russian dolls anyone?
# The bootfs this script creates is intended for use by the fuchsia installer
# program. By default the bootfs file will be called installer.bootfs and placed
# at the base of the --build_dir from whose contents the bootfs is assembled.

# path of the 'install disk image' on our the user.bootfs we're making
FUCHSIA_DISK_IMG_PATH = "installer/user_fs.lz4"
BOOTFS_PREAMBLE = "user.bootfs\n"

parser = argparse.ArgumentParser(description='Copy build files')
parser.add_argument('--temp_dir', dest='temp_dir', action='store',
                    required=False,
                    default=os.path.join(os.getcwd(), "build-installer"),
                    help='A location the script can use for temporary files')
parser.add_argument('--disk_path', action='store', required=True,
                    help="""The file to be used as disk to create an install file
                    system""")
parser.add_argument('--mcp_path', action='store', required=True,
                    help='Path to the mcopy binary')
parser.add_argument('--lz4_path', action='store', required=True,
                    help='Path to the lz4 binary')
parser.add_argument('--out_file', action='store', required=False,
                    help='Where to put the bootfs')
parser.add_argument('--build_dir', action='store', required=True,
                    help='Location of system build output')
parser.add_argument('--mkbootfs', action='store', required=False,
                    help="""Path to mkbootfs binary, if not supplied its location
                    will assumed to be relative to --build_dir""")
parser.add_argument('--file_manifest', action='store', required=False,
                    help="""Location of the primary file manifest, if not
                    provided it will be assumed to be relative to build_dir""")
parser.add_argument('--minfs_path', action='store', required=True,
                    help="The location of the host-compiled minfs binary")

args = parser.parse_args()
aux_manifest = os.path.join(args.temp_dir, "installer.manifest")

disk_path = args.disk_path
if disk_path[0] != "/":
  disk_path = os.path.join(os.getcwd(), disk_path)

mcopy_path = args.mcp_path
lz4_path = args.lz4_path

out_file = args.out_file
if out_file is None:
  out_file = os.path.join(args.build_dir, "installer.bootfs")

primary_manifest = args.file_manifest
if primary_manifest is None:
  primary_manifest = os.path.join(args.build_dir, "gen", "packages", "gn",
                                  "user.bootfs.manifest")
mkbootfs_path = args.mkbootfs
if mkbootfs_path is None:
  mkbootfs_path = os.path.join(args.build_dir, "..", "build-magenta", "tools",
                               "mkbootfs")

minfs_bin = args.minfs_path
if not os.path.exists(minfs_bin):
  print "minfs path '%s' is not found, please supply a valid path" % minfs_bin
  sys.exit(-1)

print "Copying files to disk image."
working_dir = os.getcwd()
minfs_cmd = [minfs_bin, disk_path, "cp", None, None]

# parse the manifest file and find the files to copy
file_count = 0
created_dirs = set()
with open(primary_manifest, "r") as manifest_file:
  for line in manifest_file:
    if "=" in line:
      if line.count("=") != 1:
        raise Exception("Unexpected format, too many equal signs: %d" %
                        line.count("="))
      parts = line.split("=")
      parts[0] = parts[0].strip()
      parts[1] = parts[1].strip()

      # first figure out if we need to make a directory for this file
      # This should split file name off the past
      dir_path = os.path.split(parts[0])[0]

      # track the directories we need to make for this file
      dirs_to_make = []

      if dir_path not in created_dirs:
        print "Creating directory %s" % dir_path

      # see if the directory for this file has been created
      while dir_path != "" and dir_path not in created_dirs:
        dir_path, new_dir = os.path.split(dir_path)
        dirs_to_make.append(new_dir)

      # now create the directory path
      while len(dirs_to_make) > 0:
        dir_path = os.path.join(dir_path, dirs_to_make.pop())

        mkdir_cmd = [minfs_bin, disk_path, "mkdir", "::%s" % dir_path]
        try:
          subprocess.check_call(mkdir_cmd)
        except (subprocess. CalledProcessError):
          print "Error creating directory '%s'" % dir_path
          sys.exit(-1)
        except (OSError):
          print "Unable to execute minfs"
          sys.exit(-1)

        # record that we've created this directory
        created_dirs.add(dir_path)

      targ_path = "::%s" % parts[0]
      minfs_cmd[3] = parts[1]
      minfs_cmd[4] = targ_path

      try:
        subprocess.check_call(minfs_cmd)
      except (subprocess. CalledProcessError):
        print "Error copying file %s" % parts[1]
        sys.exit(-1)
      except (OSError):
        print "Unable to execute minfs"
        sys.exit(-1)

      file_count += 1
      # some status output
      sys.stdout.write("%s\r" % (" " * 100))
      sys.stdout.write("Copying '%s' \r" % parts[0])

print "\nCopied %i files" % file_count

# lz4 compress
compressed_disk = "%s.lz4" % disk_path
if os.path.exists(compressed_disk):
  os.remove(compressed_disk)

print "Compressing system disk image to %s" % compressed_disk
lz4_cmd = [lz4_path, "-4", "-B4", disk_path, compressed_disk]
try:
  subprocess.check_call(lz4_cmd, cwd=working_dir)
except (subprocess. CalledProcessError):
  print "Error compressing disk image"
  sys.exit(-1)
except (OSError):
  print "Unable to execute LZ4"
  sys.exit(-1)

# write out a manifest file so we include the compressed file system we created
with open(aux_manifest, "w+") as manifest_file:
  manifest_file.write(BOOTFS_PREAMBLE);
  manifest_file.write("%s=%s\n" % (FUCHSIA_DISK_IMG_PATH, compressed_disk))

mkfs_cmd = [mkbootfs_path, "-c", "-o", out_file, aux_manifest, primary_manifest]

print "Creating installer bootfs"
try:
  subprocess.check_call(mkfs_cmd, cwd=working_dir)
except (subprocess. CalledProcessError):
  print "Error creating bootfs"
  sys.exit(-1)
except (OSError):
  print "Unable to execute mkfs command"
  sys.exit(-1)
