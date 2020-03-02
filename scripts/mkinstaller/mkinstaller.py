#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import argparse
import functools
import json
import os
import platform
import stat
import subprocess
import sys
import time

import paths

if sys.hexversion < 0x030700F0:
  print(
      'This script requires Python >= 3.7 to run (you have %s), please upgrade!'
      % (platform.python_version()),
      file=sys.stderr)
  sys.exit(1)

WORKSTATION_INSTALLER_GPT_GUID = '4dce98ce-e77e-45c1-a863-caf92f1330c1'

# This is the list of Fuchsia build images we write to the final image,
# and the partition types they will have (passed to cgpt)
IMAGES = {
    # This is the zedboot image, which is actually booted.
    'zedboot-efi': 'efi',
    # This is the EFI system partition that will be installed to the target.
    'efi': WORKSTATION_INSTALLER_GPT_GUID,
    'zircon-a': WORKSTATION_INSTALLER_GPT_GUID,
    'zircon-r': WORKSTATION_INSTALLER_GPT_GUID,
    'storage-sparse': WORKSTATION_INSTALLER_GPT_GUID,
}



def ParseSize(size):
  """Parse a size.

  Args:
    size: '<number><suffix>', where suffix is 'K', 'M', or 'G'.

  Returns:
    A size in bytes equivalent to the human-readable size given.
  Raises:
    A ValueError if <suffix> is unrecognised or <number> is not a base-10
    number.
  """
  units = ['K', 'M', 'G']
  if size.isdigit():
    return int(size)
  else:
    unit = size[-1].upper()
    size_bytes = int(size[:-1]) * 1024

  if unit not in units:
    raise ValueError('unrecognised unit suffix "{}" for size {}'.format(
        unit, size))

  while units.pop(0) != unit:
    size_bytes *= 1024
  return size_bytes


def PrettySize(size):
  """Returns a size in bytes as a human-readable string."""
  units = 'BKMGT'

  unit = 0
  # By the time we get to 3072, the error caused by
  # shifting units is <2%, so we don't care.
  while size > 3072 and unit < len(units) - 1:
    size /= 1024
    unit += 1

  return '{:1.1f}{}'.format(size, units[unit])


class Partition:
  """Represents a single partition to be written to the disk.

  Attributes:
    label: label of the partition on the output image.
    path: path of the file that is going to be written to the partition on
      the host.
    real_size: size of the file on the host, in bytes.
    size: size of the partition, in bytes. This may not match real_size due
      to sector size alignment or EFI partition rules.
    type: type of the partition, passed to `cgpt`.
  """
  FAT32_MIN_SIZE = (63 * 1024 * 1024)

  def __init__(self, path, part_type, label):
    self.path = path
    self.type = part_type
    self.label = label

    # Calculate sector-aligned size of this file.
    stat_result = os.stat(path)
    if not stat.S_ISREG(stat_result.st_mode):
      raise ValueError('{} is not a regular file.'.format(path))
    rounded_size = stat_result.st_size
    if rounded_size % Image.SECTOR_SIZE != 0:
      rounded_size += Image.SECTOR_SIZE
      rounded_size -= rounded_size % Image.SECTOR_SIZE
    if self.type == 'efi':
      # Gigaboot won't be able to load zedboot.bin from an EFI partition that's
      # too small, so we ensure the partition is at least big enough for it to work.
      rounded_size = max(Partition.FAT32_MIN_SIZE, rounded_size)
    self.real_size = stat_result.st_size
    self.size = rounded_size


class Image:
  """Represents a single disk image to be written.

    Attributes:
        filename: output filename of the image.
        is_usb: True if writing to a USB, False if creating an image on the
          host.
        file: filehandle to the output image. Held open while we work to prevent
          auto-mounting of USB.
        block_size: number of bytes to write at a time to the disk.
        file_size: total size of the image, in bytes
        partitions: list of |Partition| objects to write to disk.
  """
  CGPT_BIN = paths.FUCHSIA_BUILD_DIR + '/host-tools/cgpt'
  SECTOR_SIZE = 512
  GPT_SECTORS = 2048

  def __init__(self, filename, is_usb, block_size):
    self.filename = filename
    self.is_usb = is_usb
    self.file = open(filename, mode='wb')
    self.block_size = block_size

    # Allocate space for the primary and backup GPTs
    self.file_size = 2 * Image.GPT_SECTORS * Image.SECTOR_SIZE
    self.partitions = []

  def _Cgpt(self, args):
    args = [Image.CGPT_BIN] + args
    return subprocess.run(args, capture_output=True)

  def _CgptAdd(self, part, offset):
    """Add a partition to the GPT represnted by thsis |Image|.

    Args:
      part: partition to add
      offset: offset to add partition at. Must be a multiple of SECTOR_SIZE.
    Returns:
      True if add succeded, False if it failed.
    """
    if offset % Image.SECTOR_SIZE != 0:
      raise ValueError('Offset must be a multiple of SECTOR_SIZE!')
    if part.size % Image.SECTOR_SIZE != 0:
      raise ValueError('Size must be a multiple of SECTOR_SIZE!')
    size = part.size // Image.SECTOR_SIZE
    offset //= Image.SECTOR_SIZE
    ret = self._Cgpt([
        'add', '-s',
        str(size), '-t', part.type, '-b',
        str(offset), '-l', part.label, self.filename
    ])
    if ret.returncode != 0:
      print('\n'
            '======= CGPT ADD FAILED! =======\n'
            'Maybe your disk is too small?\n')
      print(ret.stdout)
      print(ret.stderr)
      return False
    return True

  def AddPartition(self, partition):
    """Add a partition to the outputted disk image.

    This function does not write any data - call Finalise() to write the
    disk image once all partitions have been added.

    Args:
      partition: partition to add
    """
    self.partitions.append(partition)
    self.file_size += partition.size

  def WritePart(self, part, offset):
    """Writes data to a partition on the output device.

    Args:
      part: partiton to write
      offset: offset in bytes to write to
    """
    self.file.seek(offset, 0)

    written = 0
    print(
        '   Writing image {} to partition {}... '.format(
            part.path.split('/')[-1], part.label),
        end=' ',
        flush=True)
    with open(part.path, 'rb') as fh:
      start = time.perf_counter()
      for block in iter(functools.partial(fh.read, self.block_size), b''):
        written += len(block)
        self.file.write(block)
      # flush and fsync to get accurate timing results
      self.file.flush()
      os.fsync(self.file.fileno())
      finish = time.perf_counter()
    per_second = written / (finish - start)
    print('{} in {:1.2f}s, {}/s'.format(
        PrettySize(written), finish - start, PrettySize(per_second)))

  def Finalise(self):
    """Write all the partitions this image represents to disk/file."""
    if not self.is_usb:
      # first, make sure the file is big enough.
      print('Create image of size={} bytes'.format(self.file_size))
      self.file.truncate(self.file_size)
      self.file.flush()
      os.fsync(self.file.fileno())

    print('Creating new GPT partition table...', end='')
    self._Cgpt(['create', self.filename])
    self._Cgpt(['boot', '-p', self.filename])
    print('done')

    print('Creating and writing partitions...')
    current_offset = Image.SECTOR_SIZE * Image.GPT_SECTORS
    for part in self.partitions:
      if not self._CgptAdd(part, current_offset):
        print('Write failed, aborting.')
        self.file.close()
        return
      self.WritePart(part, current_offset)
      current_offset += part.size
    print('Done.')
    self.file.close()


def GetPartitions():
  """Get all partitions to be written to the output image.

  The list of partitions is currently determined by the IMAGES dict
  at the top of this file.

  Returns:
    a list of |Partition| objects to be written to the disk.
  """
  images = {}
  try:
    with open(paths.FUCHSIA_BUILD_DIR + '/images.json') as f:
      images_list = json.load(f)
      for image in images_list:
        images[image['name']] = image
  except IOError as err:
    print(
        'Failed to find image manifest. Have you run `fx build`?',
        file=sys.stderr)
    print(err)
    return []

  ret = []
  for (name, part_type) in IMAGES.items():
    if name not in images:
      raise ValueError(
          'Could not find required image {} in images.json!'
          'Are you building for a platform that supports the installer?'.format(
              name))

    full_path = os.path.join(paths.FUCHSIA_BUILD_DIR, images[name]['path'])
    ret.append(Partition(full_path, part_type, name))
  return ret


def GetUsbDisks():
  """Get a list of all USB disks on the system.

  Returns:
    A list where each entry is of the format '/path/to/disk - <disk name>'
  """
  res = subprocess.run(['fx', 'list-usb-disks'], capture_output=True)
  res.check_returncode()

  disks = res.stdout.decode('utf-8').split('\n')
  return disks


def IsUsbDisk(path):
  """Is the given path a USB disk?

  Args:
    path: a path that may represent a USB disk. Does not have to exist.

  Returns:
    True if the path represents a USB disk, False otherwise.
  """
  return path in map(lambda a: a.split()[0], GetUsbDisks())


def EjectDisk(path):
  """Eject the given USB disk from the system."""
  system = platform.system()
  if system == 'Linux':
    subprocess.run(['eject', path])
  elif system == 'Darwin':
    subprocess.run(['diskutil', 'eject', path])
  print('Ejected USB disk')


def Main(args):
  path = args.FILE
  if args.create:
    if not args.force and os.path.exists(path):
      print(
          'File {} already exists, not creating an image. Use --force if you want to proceed.'
          .format(path),
          file=sys.stderr)
      return 1
  else:
    if not os.path.exists(path):
      print(
          ('Path {} does not exist, use --create to create a disk image.\n'
           'Detected USB devices:\n'
           '{}').format(path, '\n'.join(GetUsbDisks())),
          end='',
          file=sys.stderr)
      return 1
    if not IsUsbDisk(path):
      print(
          ('Path {} is not a USB device. Use -f to force.\n'
           'Detected USB devices:\n'
           '{}').format(path, '\n'.join(GetUsbDisks())),
          end='',
          file=sys.stderr)
      return 1

    if not os.access(path, os.W_OK):
      print('Changing ownership of {} to {}'.format(path,
                                                    os.environ.get('USER')))
      subprocess.run(
          ['sudo', 'chown', os.environ.get('USER'), path],
          stdin=sys.stdin,
          stdout=sys.stdout,
          stderr=sys.stderr)

  parts = GetPartitions()
  if not parts:
    return 1

  output = Image(args.FILE, not args.create, ParseSize(args.block_size))
  for p in parts:
    output.AddPartition(p)

  output.Finalise()
  if not args.create:
    EjectDisk(path)
  return 0


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
      description='Create a Fuchsia installer image.', prog='fx mkinstaller')
  parser.add_argument(
      '-c',
      '--create',
      action='store_true',
      help='Create a disk image instead of writing to an existing disk.')
  parser.add_argument(
      '-f',
      '--force',
      action='store_true',
      help='Force writing to an image that already exists or a disk that might not be a USB.'
  )
  parser.add_argument(
      '-b',
      '--block-size',
      type=str,
      default='2M',
      help='Block size (optionally suffixed by K, M, G) to write. Default is 2M'
  )
  parser.add_argument('FILE', help='Path to USB device or installer image')
  argv = parser.parse_args()
  sys.exit(Main(argv))
