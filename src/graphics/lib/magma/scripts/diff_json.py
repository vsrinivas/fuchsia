#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import difflib
import json
import sys

def usage():
  print('Usage:\n'
        '  diff_json.py FILE1 FILE2 OUTPUT\n'
        '  Generates canonical json text for FILE1 and FILE2, then does a normal text comparison,\n'
        '  writing the diff output to OUTPUT.')

def main():
  if (len(sys.argv) != 4):
    usage()
    exit(-1)
  try:
    with open(sys.argv[1], 'r') as file1:
      with open(sys.argv[2], 'r') as file2:
        with open(sys.argv[3], 'w') as result:
          json1 = json.load(file1)
          json2 = json.load(file2)
          canon1 = json.dumps(json1, sort_keys=True, indent=2).splitlines()
          canon2 = json.dumps(json2, sort_keys=True, indent=2).splitlines()
          diff = difflib.unified_diff(canon1, canon2, sys.argv[1], sys.argv[2], lineterm='')
          diffstr = '\n'.join(diff)
          result.write(diffstr)
          if (len(diffstr) != 0):
            print(f'Error: non-empty diff between canonical json representations:\n{diffstr}')
            exit(-4)
  except IOError as e:
    print(f'Error accessing files: {e}')
    usage()
    exit(-2)
  except ValueError as e:
    print(f'Error decoding json: {e}')
    exit(-3)

if __name__ == '__main__':
  sys.exit(main())
