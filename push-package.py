#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
import json
import os
import shutil
import subprocess
import sys
import tempfile

DEFAULT_DST_ROOT = '/system'
DEFAULT_OUT_DIR = 'out/debug-x86-64'


def netaddr_cmd(out_dir, device):
  path = os.path.join(out_dir, '../build-magenta/tools/netaddr')
  command = [
      path,
      '--fuchsia',
      device,
  ]
  return command


def mkdir_p(path):
  try:
    os.makedirs(path)
  except OSError as exc:  # Python >2.5
    if exc.errno == errno.EEXIST and os.path.isdir(path):
      pass
    else:
      raise


def parse_package_file(package_file_path):
  with open(package_file_path) as package_file:
    data = json.load(package_file)
  return data


def scp_everything(package_data, out_dir, dst_root, name_filter, verbose):
  tar_dir = os.path.join(out_dir, 'push_package_tmp')
  dst_dir = os.path.join(tar_dir, dst_root.lstrip('/'))
  ssh_config_path = os.path.join(out_dir, 'ssh-keys', 'ssh_config')
  if verbose:
    tar_extract_flags = '-xvf'
  else:
    tar_extract_flags = '-xf'

  # Clean up if a previous run exited improperly
  shutil.rmtree(dst_dir, ignore_errors=True)

  # Temporary file for tar.
  with tempfile.TemporaryFile() as f:
    try:
      # Create a directory tree that mirrors what we want on the device.
      for binary in package_data['binaries']:
        binary_name = binary['binary']
        if name_filter is not None and name_filter not in binary_name:
          continue

        src_path = os.path.join(out_dir, binary_name)
        dst_path = os.path.join(dst_dir, binary['bootfs_path'].lstrip('/'))
        device_path = os.path.join(dst_root,
                                   binary['bootfs_path'].lstrip('/'))
        if (verbose):
          print 'Copying "%s"\n     => "%s"' % (src_path, device_path)
        mkdir_p(os.path.dirname(dst_path))
        os.link(src_path, dst_path)

      # tar the new directory tree to our temp file.
      status = subprocess.call(
          ['tar', '-cf', '-', dst_root.lstrip('/')], cwd=tar_dir,
          stdout=f)
      if status != 0:
        sys.stderr.write('error: tar failed\n')
        return status
    finally:
      shutil.rmtree(tar_dir, ignore_errors=True)

    try:
      netaddr = netaddr_cmd(out_dir, ':')
      host = subprocess.check_output(netaddr).strip()
    except subprocess.CalledProcessError:
      sys.stderr.write('error: device "%s" not found.\n', ':')
      return 1

    f.seek(0, 0)
    status = subprocess.call(
        ['ssh', '-F', ssh_config_path, host, 'tar', tar_extract_flags, '-'],
        stdin=f)
    if status != 0:
      sys.stderr.write('error: ssh failed\n')
      return status

  print 'Updated %d files on device' % (len(package_data['binaries']))

  return 0


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('package_file', help='JSON file containing package data')
  parser.add_argument(
      '-o',
      '--out-dir',
      metavar='DIR',
      default=DEFAULT_OUT_DIR,
      help='Directory containing build products')
  parser.add_argument(
      '-d',
      '--dst-root',
      metavar='PATH',
      default=DEFAULT_DST_ROOT,
      help='Path to the directory to copy package products')
  parser.add_argument(
      '-f',
      '--filter',
      metavar='FILTER',
      help='Push products with a name that contains FILTER')
  parser.add_argument(
      '-v', '--verbose', action='store_true', help='Display copy filenames')
  args = parser.parse_args()

  package_data = parse_package_file(args.package_file)
  out_dir = args.out_dir or DEFAULT_OUT_DIR
  dst_root = args.dst_root or DEFAULT_DST_ROOT
  name_filter = args.filter
  verbose = args.verbose

  return scp_everything(package_data, out_dir, dst_root, name_filter, verbose)


if __name__ == '__main__':
  sys.exit(main())
