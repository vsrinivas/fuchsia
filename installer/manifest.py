#!/usr/bin/env python

"""
Copyright 2017 The Fuchsia Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.

This modules provides an interface to build minfs images from fuchsia
manifest files.

"""

import os
import stat
import subprocess
import sys

def _get_file_len(path):
    """Returns the length of an input file in bytes. """
    fd = os.open(path, os.O_RDONLY)
    try:
        return os.lseek(fd, 0, os.SEEK_END)
    finally:
        os.close(fd)

def process_manifest(manifest_path, disk_path, minfs_bin, minfs_cmd, created_dirs):
    file_count = 0
    with open(manifest_path, "r") as manifest_file:
        for line in manifest_file:
            if "=" not in line:
                continue

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
                except (subprocess.CalledProcessError):
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
            except (subprocess.CalledProcessError):
                print "Error copying file %s command %s" % (parts[1], minfs_cmd)
                sys.exit(-1)
            except (OSError):
                print "Unable to execute minfs"
                sys.exit(-1)

            file_count += 1
            # some status output
            sys.stdout.write("%s\r" % (" " * 100))
            sys.stdout.write("Copying '%s' \r" % parts[0])
            sys.stdout.flush()
    return file_count

def build_minfs_image(manifests, minfs_image_path, minfs_bin):
    """Populate a minfs container with the contents specified by the fuchsia
       manifest files.
    """

    # Determine the length of the minfs image. Note that this works on
    # regular files as well as block devices so it should allow us to
    # use the minfs tool to manipulate block devices.
    minfs_image_len = _get_file_len(minfs_image_path)
    disk_path = "%s@%d" % (minfs_image_path, minfs_image_len)

    minfs_cmd = [minfs_bin, disk_path, "cp", None, None]

    # parse the manifest files and find the files to copy
    file_count = 0
    created_dirs = set()
    for manifest_path in manifests:
        file_count += process_manifest(manifest_path, disk_path, minfs_bin,
                minfs_cmd, created_dirs)

    return file_count
