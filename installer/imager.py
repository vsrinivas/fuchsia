#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import subprocess
import sys

import manifest

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
EFI_DISK_IMG_PATH = "installer/efi_fs.lz4"
DIR_EFI = "EFI"
DIR_EFI_BOOT = "EFI/BOOT"
FILE_KERNEL = "zircon.bin"
FILE_KERNEL_RD = "ramdisk.bin"
FILE_KERNEL_CMDLINE = "cmdline"

def compress_file(lz4_path, source, dest, working_dir):
  lz4_cmd = [lz4_path, "-4", "-B4", source, dest]

  if os.path.exists(dest):
    os.remove(dest)

  try:
    subprocess.check_call(lz4_cmd, cwd=working_dir)
  except (subprocess.CalledProcessError):
    print "Error compressing file %s" % source
    return False
  except (OSError):
    print "Unable to execute %s" % lz4_path
    return False

  return True

def cp_fat(mcopy_path, mdir_path, target_disk, local_path, remote_path, working_dir):
  mdir_cmd = [mdir_path, "-i", target_disk, "::%s" % remote_path]

  with open('/dev/null', 'w') as f:
    try:
      subprocess.check_call(mdir_cmd, cwd=working_dir, stdout=f, stderr=f)
      print "File '%s' already exists." % remote_path
      return False
    except (subprocess.CalledProcessError):
      pass
    except (OSError):
      print "Unable to execute %s" % mdir_path
      return False

  mcpy_cmd = [mcopy_path, "-i", target_disk, local_path, "::%s" % remote_path]

  try:
    subprocess.check_call(mcpy_cmd, cwd=working_dir)
  except (subprocess.CalledProcessError):
    print "Error copying %s" % local_path
    return False
  except (OSError):
    print "Unable to execute %s" % mcopy_path
    return False

  return True

def mkdir_fat(mmd_path, target_disk, remote_path, working_dir):
  mmd_cmd = [mmd_path, "-i", target_disk, "::%s" % remote_path]

  try:
    subprocess.check_call(mmd_cmd, cwd=working_dir)
  except (subprocess.CalledProcessError):
    print "Error making directory %s" % remote_path
    return False
  except (OSError):
    print "Unable to execute %s" % mmd_path
    return False

  return True

def build_manifest_lists(dir):
  sys_manifests = []
  boot_manifests = []

  gen_dir = os.path.join(dir, "gen", "packages", "gn")
  pkg_list = os.path.join(gen_dir, "packages")
  with open(pkg_list) as pkg_list_file:
    for pkg_name in pkg_list_file:
      pkg_name = pkg_name.rstrip()
      pkg_dir = os.path.join(dir, "package", pkg_name)

      pkg_sys_man = os.path.join(pkg_dir, "system_manifest")
      if is_non_empty_file(pkg_sys_man):
        sys_manifests.append(pkg_sys_man)

      pkg_boot_man = os.path.join(pkg_dir, "boot_manifest")
      if is_non_empty_file(pkg_boot_man):
        boot_manifests.append(pkg_boot_man)

  return sys_manifests, boot_manifests

def mk_bootdata_fs(bin_path, out_path, working_dir, orig_bootdata, aux_manifests):
  if len(aux_manifests) != 0:
    bootdata_mkfs_cmd = [bin_path, "-c", "--target=boot", "-o", out_path,
                         orig_bootdata] + aux_manifests
    subprocess.check_call(bootdata_mkfs_cmd, cwd=working_dir)
    return out_path
  else:
    return orig_bootdata

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
parser.add_argument('--mmd_path', action='store', required=True,
                    help='Path to mmd binary')
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
parser.add_argument('--boot_manifest', action='store', required=False,
                    help="""Location of the boot partition image manifest, if
                    not provided it will be assumed to be relative to build_dir""")
parser.add_argument('--minfs_path', action='store', required=True,
                    help="The location of the host-compiled minfs binary")
parser.add_argument('--build_dir_zircon', action='store', required=False,
                    help="""Directory in which to find magneta build artifacts.
                    either this or both the kernel AND efi_loader args must be
                    supplied.""")
parser.add_argument('--kernel', action='store', required=False,
                    help="""Location of zircon.bin, if not supplied this will
                    be assumed to be relative to --build_dir""")
parser.add_argument('--efi_loader', action='store', required=False,
                    help="""Location of the kernel bootloader loaded by EFI
                    (usually boot{arch}.efi). If not supplied this is assumed
                    to be relative to --build_dir""")
parser.add_argument('--efi_disk', action='store', required=True,
                    help="Location of file to use to create the ESP disk image")
parser.add_argument('--arch', action='store', required=False,
                    help="""The CPU architecture of the device, if not supplied
                    x86-64 is assumed""")
parser.add_argument('--bootdata', action='store', required=False,
                    help="""The kernel RAM disk file, if not supplied this is
                    assumed to be relative to --build_dir""")
parser.add_argument('--kernel_cmdline', action='store', required=False,
                    help="""Path to a file with kernel command line options""")
parser.add_argument('--disable_thread_exp', action='store_const',
                    required=False, default=False, const=True,
                    help="Whether to disable experimental thread priorization")
parser.add_argument('--mdir_path', action='store', required=True,
                    help='Path to mdir binary')
parser.add_argument('--runtime_dir', action='store', required=False,
                    help="""Path to the output directory containing the runtime
                    available when running the installer""")

args = parser.parse_args()
disk_path_efi = args.efi_disk
bootloader = args.efi_loader
kernel = args.kernel
build_dir_zircon = args.build_dir_zircon
bootdata = args.bootdata
kernel_cmdline = args.kernel_cmdline
enable_thread_exp = not args.disable_thread_exp
mdir_path = args.mdir_path

runtime_dir = args.runtime_dir

arch = args.arch
if arch is None:
  arch = "X64"

if not runtime_dir:
  runtime_dir = args.build_dir

# if bootloader was not supplied, find it relative to the zircon build dir
if bootloader is None:
  if build_dir_zircon is not None:
    bootloader = os.path.join(build_dir_zircon, "bootloader", "bootx64.efi")
  else:
    print """You must supply either the zircon build dir or the path to
    the EFI bootloader"""
    sys.exit(-1)

# if the kernel was not supplied, find it relative to the zircon build dir
if kernel is None:
  if build_dir_zircon is not None:
    kernel = os.path.join(build_dir_zircon, "zircon.bin")
  else:
    print """You must supply either the zircon build dir or the path to
    the kernel"""
    sys.exit(-1)

# if the kernel ram disk was not supplied, find it relative to the zircon build
# dir
if not bootdata:
  if build_dir_zircon is not None:
    bootdata = os.path.join(build_dir_zircon, "bootdata.bin")
  else:
    print """You must supply either the zircon build dir or the path
    to the bootdata.bin"""
    sys.exit(-1)

runtime_bootdata = bootdata

if arch == "X64" and not os.path.exists(bootloader):
  print """EFI loader does not exist at path %s, please check the path and try
  again.""" % bootloader
  sys.exit(-1)

if not os.path.exists(kernel):
  print """kernel does not exist at path %s, please check the path and try
  again.""" % kernel
  sys.exit(-1)

aux_manifest = os.path.join(args.temp_dir, "installer.manifest")

disk_path = args.disk_path
if disk_path[0] != "/":
  disk_path = os.path.join(os.getcwd(), disk_path)

mcopy_path = args.mcp_path
lz4_path = args.lz4_path
mmd_path = args.mmd_path

out_file = args.out_file
if out_file is None:
  out_file = os.path.join(args.build_dir, "installer.bootfs")

build_gen_dir = os.path.join(args.build_dir, "gen", "packages", "gn")

primary_manifest = args.file_manifest
if primary_manifest is None:
  primary_manifest = os.path.join(build_gen_dir, "system.bootfs.manifest")
boot_manifest = args.boot_manifest
if not boot_manifest:
  boot_manifest = os.path.join(build_gen_dir, "boot.bootfs.manifest")
package_list = os.path.join(build_gen_dir, "packages")

mkbootfs_path = args.mkbootfs
if mkbootfs_path is None:
  mkbootfs_path = os.path.join(args.build_dir, "..", "build-zircon", "tools",
                               "mkbootfs")

minfs_bin = args.minfs_path
if not os.path.exists(minfs_bin):
  print "minfs path '%s' is not found, please supply a valid path" % minfs_bin
  sys.exit(-1)
# FILE_BOOTLOADER = "%s/BOOTX64.EFI" % DIR_EFI_BOOT

if arch != "X64" and arch != "ARM" and arch != "AA64":
  print "Architecture '%s' is not recognized" % arch
  sys.exit(-1)

bootloader_remote_path = "%s/BOOT%s.EFI" % (DIR_EFI_BOOT, arch)

print "Copying files to disk image."
working_dir = os.getcwd()

def is_non_empty_file(path):
  return os.path.exists(path) and os.path.getsize(path) > 0

# Take the files referenced by primary_manifest and each package's system
# manifests and write them into the minfs image at disk_path using the minfs
# binary pointed to by minfs_bin.
system_manifests, boot_manifests = build_manifest_lists(args.build_dir)
if is_non_empty_file(primary_manifest):
  system_manifests.append(primary_manifest)

if is_non_empty_file(boot_manifest):
    boot_manifests.append(boot_manifest)

file_count = manifest.build_minfs_image(system_manifests, disk_path, minfs_bin)

print "\nCopied %i files" % file_count

compressed_disk = "%s.lz4" % disk_path
sparse_disk = "%s.sparse" % disk_path

sparse_cmd = [os.path.join(args.build_dir, "host_x64", "sparser"), "-s", disk_path, sparse_disk]
subprocess.check_call(sparse_cmd, cwd=working_dir)

print "Compressing system disk image to %s" % compressed_disk
if not compress_file(lz4_path, sparse_disk, compressed_disk, working_dir):
  sys.exit(-1)

# create /EFI/BOOT
if not (mkdir_fat(mmd_path, disk_path_efi, DIR_EFI, working_dir) and
        mkdir_fat(mmd_path, disk_path_efi, DIR_EFI_BOOT, working_dir)):
  sys.exit(-1)

bootdata = mk_bootdata_fs(mkbootfs_path,
                          os.path.join(args.build_dir, "installer.bootdata.bootfs"),
                          working_dir, bootdata, boot_manifests)

if not (cp_fat(mcopy_path, mdir_path, disk_path_efi, bootloader,
               bootloader_remote_path, working_dir)
        and
        cp_fat(mcopy_path, mdir_path, disk_path_efi, kernel, FILE_KERNEL,
               working_dir)
        and
        cp_fat(mcopy_path, mdir_path, disk_path_efi, bootdata, FILE_KERNEL_RD,
               working_dir)):
  sys.exit(-1)

if not kernel_cmdline:
  kernel_cmdline = os.path.join(args.temp_dir, "kernel_cmdline")
  if os.path.exists(kernel_cmdline):
    os.remove(kernel_cmdline)

with open(kernel_cmdline, "a+") as kcmd:
  kcmd.write(" %s=%s" % ("thread.set.priority.allowed",
                         "true" if enable_thread_exp else "false"))

if not cp_fat(mcopy_path, mdir_path, disk_path_efi, kernel_cmdline, FILE_KERNEL_CMDLINE, working_dir):
  print "Could not copy kernel cmdline"
  sys.exit(-1)

print "Copied command line \"%s\"" % kernel_cmdline

compressed_disk_efi = "%s.lz4" % disk_path_efi
sparse_disk = "%s.sparse" % disk_path_efi

sparse_cmd = [os.path.join(args.build_dir, "host_x64", "sparser"), "-s", disk_path_efi, sparse_disk]
subprocess.check_call(sparse_cmd, cwd=working_dir)

print "Compressing ESP disk image to %s" % compressed_disk_efi
if not compress_file(lz4_path, sparse_disk, compressed_disk_efi, working_dir):
  sys.exit(-1)

runtime_sys_manifests, runtime_boot_manifests = build_manifest_lists(runtime_dir)
gen_dir = os.path.join(runtime_dir, "gen", "packages", "gn")
runtime_sys_man = os.path.join(gen_dir, "system.bootfs.manifest")
if is_non_empty_file(runtime_sys_man):
  runtime_sys_manifests.append(runtime_sys_man)
runtime_boot_man = os.path.join(gen_dir, "boot.bootfs.manifest")
if is_non_empty_file(runtime_boot_man):
  runtime_boot_manifests.append(runtime_boot_man)
runtime_bootdata = mk_bootdata_fs(mkbootfs_path,
                                  os.path.join(runtime_dir, "installer.bootdata.bootfs"),
                                  working_dir, runtime_bootdata, runtime_boot_manifests)

# write out a manifest file so we include the compressed file system we created
with open(aux_manifest, "w+") as manifest_file:
  manifest_file.write(BOOTFS_PREAMBLE);
  manifest_file.write("%s=%s\n" % (FUCHSIA_DISK_IMG_PATH, compressed_disk))
  manifest_file.write("%s=%s\n" % (EFI_DISK_IMG_PATH, compressed_disk_efi))

mkfs_cmd = [mkbootfs_path, "-c", "--target=system", "-o", out_file, runtime_bootdata,
            aux_manifest] + runtime_sys_manifests

print "Creating installer bootfs"
try:
  subprocess.check_call(mkfs_cmd, cwd=working_dir)
except (subprocess.CalledProcessError):
  print "Error creating bootfs"
  sys.exit(-1)
except (OSError):
  print "Unable to execute mkfs command"
  sys.exit(-1)

# shift around the user.bootfs files
user_bootfs = os.path.join(os.path.dirname(out_file), "user.bootfs")
os.rename(user_bootfs, os.path.join(os.path.dirname(user_bootfs), "user-noinstaller.bootfs"))
os.rename(out_file, user_bootfs)
