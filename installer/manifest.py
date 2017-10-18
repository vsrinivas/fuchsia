#!/usr/bin/env python

"""
Copyright 2017 The Fuchsia Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.

This module provides an interface to build minfs images from fuchsia
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

def process_manifest(manifest_path, disk_path, minfs_bin, created_dirs):
    mkdir_cmd = [minfs_bin, disk_path, "mkdir", None]
    cp_cmd = [minfs_bin, disk_path, "cp", None, None]
    ls_cmd = [minfs_bin, disk_path, "ls", None]

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

                mkdir_cmd[3] = "::%s" % dir_path
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

            # some status output
            sys.stdout.write("%s\r" % (" " * 100))
            sys.stdout.write("Copying '%s' \r" % parts[0])
            sys.stdout.flush()
            ls_cmd[3] = targ_path

            try:
              subprocess.check_call(ls_cmd)
              print "File '%s' from '%s' is included twice." % (targ_path, parts[1])
              sys.exit(-1)
            except (subprocess.CalledProcessError):
              pass
            except (OSError):
              print "Unable to execute minfs"
              sys.exit(-1)

            cp_cmd[3] = parts[1]
            cp_cmd[4] = targ_path
            try:
                subprocess.check_call(cp_cmd)
            except (subprocess.CalledProcessError):
                print "Error copying file %s command %s" % (parts[1], cp_cmd)
                sys.exit(-1)
            except (OSError):
                print "Unable to execute minfs"
                sys.exit(-1)

            file_count += 1
    return file_count

def mkfs(minfs_bin, path):
    try:
        mkfs_cmd = [minfs_bin, path, "mkfs"]
        subprocess.check_call(mkfs_cmd)
    except:
        print "Unable to mkfs minfs partition"
        sys.exit(-1)

def build_minfs_image(manifests, minfs_image_path, minfs_bin):
    """Populate a minfs container with the contents specified by the fuchsia
       manifest files.
    """

    # If the minfs image does not already exist, create it as a file
    if not os.path.isfile(minfs_image_path):
        total_sz = 0
        for path in manifests:
            with open(path, "r") as file:
                for line in file:
                    hostpath = line.split("=")[1].strip()
                    total_sz += os.path.getsize(hostpath)
        print "Manifest File Size (summation): %d" % total_sz

        # Provide some extra wiggle room for minfs metadata
        total_sz = int(float(total_sz) * 1.4) + (1 << 20)
        # Round up to the nearest 4KB
        total_sz += (4096 - 1)
        total_sz -= (total_sz % 4096)
        print "Size of Generated minfs image: %d" % total_sz

        with open(minfs_image_path, "w") as partition:
            partition.truncate(total_sz)

    # Determine the length of the minfs image. Note that this works on
    # regular files as well as block devices so it should allow us to
    # use the minfs tool to manipulate block devices.
    minfs_image_len = _get_file_len(minfs_image_path)
    disk_path = "%s@%d" % (minfs_image_path, minfs_image_len)
    mkfs(minfs_bin, disk_path)

    # parse the manifest files and find the files to copy
    file_count = 0
    created_dirs = set()
    for manifest_path in manifests:
        file_count += process_manifest(manifest_path, disk_path, minfs_bin,
                                       created_dirs)

    return file_count
