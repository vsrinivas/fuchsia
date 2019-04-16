#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import subprocess
import sys

CATEGORIES = [
  'dart-codespace',
  'dart-oldspace',
  'dart-newspace',
  'jemalloc-heap',
  'pthread_t',
  'magma_create_buffer',
  'scudo:primary',
  'scudo:secondary',
  'lib',
]

def FxSSH(address, command):
  fx = os.path.join(paths.FUCHSIA_ROOT, 'scripts', 'fx')
  cmd = [fx, 'ssh', address] + command
  try:
    result = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError as e:
    print ("command failed: " + ' '.join(cmd) + "\n" +
           "output: " + e.output)
    return None
  return result


def HumanToBytes(size_str):
  last = size_str[-1]
  KB = 1024
  if last == 'B':
    multiplier = 1
  elif last == 'k':
    multiplier = KB
  elif last == 'M':
    multiplier = KB * KB
  elif last == 'G':
    multiplier = KB * KB * KB
  elif last == 'T':
    multiplier = KB * KB * KB * KB
  else:
    raise Exception('Unknown multiplier ' + last)
  return float(size_str[:-1]) * multiplier


def BytesToHuman(num, suffix='B'):
  for unit in ['','Ki','Mi','Gi','Ti','Pi','Ei','Zi']:
    if abs(num) < 1024.0:
      return "%3.1f%s%s" % (num, unit, suffix)
    num /= 1024.0
  return "%.1f%s%s" % (num, 'Yi', suffix)


# The output of vmos is:
# rights  koid parent #chld #map #shr    size   alloc name [app]
# on each line
def ParseVmos(vmos, matchers):
  vmo_lines = vmos.strip().split('\n')
  sizes = {}
  koids = {}
  for vmo in vmo_lines:
    # 1: koid, 5: process sharing, 6: size, 7: alloc, 8: name [9: app]
    data = vmo.split()
    if len(data) < 9:
      continue
    name = data[8]
    if len(data) >= 10:
      name = name + ' ' + data[9]
    try:
      b = HumanToBytes(data[7])
    except:
      continue
    koid = int(data[1])
    if koid in koids:
      continue
    koids[koid] = True
    sharing = int(data[5])
    if sharing == 0:
      continue
    for matcher in matchers:
      if matcher not in name:
        continue
      if matcher in sizes:
        sizes[matcher] = sizes[matcher] + (b / sharing)
      else:
        sizes[matcher] = (b / sharing)
      break
    if 'total' in sizes:
      sizes['total'] = sizes['total'] + (b / sharing)
    else:
      sizes['total'] = (b / sharing)
  return sizes


def Main():
  parser = argparse.ArgumentParser('Display stats about Dart VMOs')
  parser.add_argument('--pid', '-p',
      required=True,
      help='pid of the target process.')
  parser.add_argument('--address', '-a',
      required=True,
      help='ipv4 address of the target device')
  args = parser.parse_args()

  vmos = FxSSH(args.address, ['vmos', args.pid])
  sizes = ParseVmos(vmos, CATEGORIES)
  for k, v in sizes.iteritems():
    print k + ", " + BytesToHuman(v)

  return 0


if __name__ == '__main__':
  sys.exit(Main())
