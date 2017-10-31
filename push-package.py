#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import errno
import json
import os
import subprocess
import sys
import tempfile

DEFAULT_DST_ROOT = '/system'
DEFAULT_OUT_DIR = 'out/debug-x86-64'


def netaddr_cmd(out_dir, device):
  path = os.path.join(out_dir, '../build-zircon/tools/netaddr')
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


def parse_package_manifest(paths, dst_root):
  data = []
  for path in paths:
    with open(path) as package_manifest:
      for line in package_manifest:
        items = line.rstrip().split('=')
        if len(items) != 2:
          raise ValueError('Malformed manifest entry: ' + line)

        dst = os.path.join(dst_root, items[0].lstrip('/'))
        src = items[1]
        data.append([dst, src])
  return data


def update_device(device, batch_file, verbose, out_dir):
  ssh_config_path = os.path.join(out_dir, 'ssh-keys', 'ssh_config')

  try:
    netaddr = netaddr_cmd(out_dir, device)
    ipv6 = '[' + subprocess.check_output(netaddr).strip() + ']'
  except subprocess.CalledProcessError:
    # netaddr prints its own errors, no need to add another one here.
    return 1

  with open(os.devnull, 'w') as devnull:
    status = subprocess.call(
        ['sftp', '-F', ssh_config_path, '-b', batch_file, ipv6],
        stdout=sys.stdout if verbose else devnull)
    if status != 0:
      print >> sys.stderr, 'error: sftp failed'

    return status


def scp_everything(devices, package_data, out_dir, name_filter, verbose):
  # Temporary file for sftp
  count = 0
  with tempfile.NamedTemporaryFile() as f:
    # Create a directory tree that mirrors what we want on the device.
    for entry in package_data:
      dst_path = entry[0]
      src_path = entry[1]

      if name_filter is not None and name_filter not in os.path.basename(
          dst_path):
        continue

      # must "rm" the file first because memfs requires it
      print >> f, '-rm %s' % dst_path
      print >> f, 'put -P %s %s' % (src_path, dst_path)

      count += 1

    f.flush()

    for device in devices:
      if update_device(device, f.name, verbose, out_dir) == 0:
        print 'Updated %d files on "%s".' % (count, device)
      else:
        print 'Update FAILED on "%s"' % device

  return 0


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      'package_files',
      nargs='+',
      help='Files containing manifest data. For example, ' \
           '(e.g. out/debug-x86-64/package/modular*/system_manifest)')
  parser.add_argument('-d', '--device', default=[':'], help='Device to update')
  parser.add_argument(
      '-o',
      '--out-dir',
      metavar='DIR',
      default=DEFAULT_OUT_DIR,
      help='Directory containing build products')
  parser.add_argument(
      '-t',
      '--dst-root',
      metavar='PATH',
      default=DEFAULT_DST_ROOT,
      help='Path on device to the directory to copy package products')
  parser.add_argument(
      '-f',
      '--filter',
      metavar='FILTER',
      help='Push products with a name that contains FILTER')
  parser.add_argument(
      '-v', '--verbose', action='store_true', help='Display copy filenames')

  args = parser.parse_args()

  out_dir = args.out_dir or DEFAULT_OUT_DIR
  dst_root = args.dst_root or DEFAULT_DST_ROOT
  name_filter = args.filter
  verbose = args.verbose

  package_data = parse_package_manifest(args.package_files, dst_root)

  return scp_everything(args.device, package_data, out_dir, name_filter,
                        verbose)


if __name__ == '__main__':
  sys.exit(main())
