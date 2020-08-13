#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# Lint as: python3

import argparse
import json
import sys

def main():
  parser = argparse.ArgumentParser(
      'Validate component manifests against package manifests',
      fromfile_prefix_chars='@')
  parser.add_argument(
      '--component_manifests',
      required=True,
      type=argparse.FileType('r'),
      nargs='+',
      help='Path to a component manifest to validate (cmx/cml file)')
  parser.add_argument(
      '--package_manifest',
      required=True,
      type=argparse.FileType('r'),
      help='Path to a package manifest to validate against')
  parser.add_argument(
      '--stamp',
      required=True,
      type=argparse.FileType('w'),
      help='Stamp file')
  args = parser.parse_args()

  dsts = [dst for dst, src in
          (line.split('=') for line in args.package_manifest.readlines())]

  for component_manifest in args.component_manifests:
    if not component_manifest.name.endswith('.cmx'):
      # Known issue: `json` can't parse JSON5 (.cml files).
      # Need to address this in the future, or after this script is
      # productionized into ffx.
      continue

    cm = json.load(component_manifest)

    if "runner" in cm:
      # Runners can change the namespace (for instance guest_runner), and can
      # generally reinterpret this part of the manifest, so we won't judge.
      continue

    try:
      binary = cm["program"]["binary"]
    except:
      # Nothing to validate
      continue

    if binary in dsts:
      continue

    # Legacy package.gni supports the "disabled test" feature that intentionally
    # breaks component manifests. /shrug
    if binary.startswith("test/") and "test/disabled/" + binary[5:] in dsts:
      continue

    print(f'Error found in {component_manifest.name}')
    print(f'program.binary="{binary}" but {binary} is not in the package!')
    print()
    nearest = nearest_match(binary, dsts)
    if nearest:
      print(f'Did you mean "{nearest}"?')
      print()
    print('Try any of the following:')
    print('\n'.join(sorted(dsts)))
    return 1

  args.stamp.write('Success!')
  return 0

def nearest_match(start, candidates):
  """Finds the nearest match to `start` out of `candidates`."""
  nearest = None
  min_distance = sys.maxsize
  for candidate in candidates:
    distance = minimum_edit_distance(start, candidate)
    if distance < min_distance:
      min_distance = distance
      nearest = candidate
  return nearest

def minimum_edit_distance(s, t):
  """Finds the Levenshtein distance between `s` and `t`."""
  # Dynamic programming table
  rows = len(s) + 1
  cols = len(t) + 1
  dist = [[0 for x in range(cols)] for x in range(rows)]

  # Fastest way to transform to empty string is N deletions
  for i in range(1, rows):
      dist[i][0] = i
  # Fastest way to transform from empty string is N insertions
  for i in range(1, cols):
    dist[0][i] = i

  for col in range(1, cols):
      for row in range(1, rows):
          if s[row-1] == t[col-1]:
              cost = 0
          else:
              cost = 1
          dist[row][col] = min(dist[row-1][col] + 1,      # Deletion
                               dist[row][col-1] + 1,      # Insertion
                               dist[row-1][col-1] + cost) # Substitution

  return dist[row][col]


if __name__ == '__main__':
  sys.exit(main())
