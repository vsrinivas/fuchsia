#!/usr/bin/env python3

"""
This script will
1. Read the ~/.fuchsia/debug/symbol-index to load the symbol lookup paths.
2. Extract build ids from *.in files under the same directory.
3. For each build id, locate the symbol file and copy it to //prebuilt/test_data/symbolizer/symbols.

To make it simpler, this script takes no argument, supports only build_id directories and hard-codes
all paths.
"""

import os
import sys


class SymbolIndex:
    _SYMBOL_INDEX = os.path.expanduser('~/.fuchsia/debug/symbol-index')

    def __init__(self):
        self._build_id_dirs = []
        with open(self._SYMBOL_INDEX) as f:
            for line in f:
                symbol_path = line.strip().split('\t')[0]
                if os.path.isdir(symbol_path):
                    self._build_id_dirs.append(symbol_path)

    def lookup(self, build_id):
        if len(build_id) < 2:
            return
        for build_id_dir in self._build_id_dirs:
            path = build_id_dir + '/' + build_id[:2] + '/' + build_id[2:] + '.debug'
            if os.path.exists(path):
                return path


def scan_build_ids(in_file):
    with open(in_file) as f:
        for line in f:
            # extract all {{{...}}} content
            start = line.find('{{{')
            if start < 0:
                continue
            end = line.find('}}}', start + 3)
            if end < 0:
                continue
            content = line[start+3:end].split(':')
            if not content:
                continue
            if content[0] == 'module':
                yield content[-1]


def copy_symbols(this_dir, target_dir):
    symbol_index = SymbolIndex()
    for in_file in os.listdir(this_dir):
        if not in_file.endswith('.in'):
            continue
        for build_id in scan_build_ids(this_dir + '/' + in_file):
            if len(build_id) < 2:
                continue
            target_parent = target_dir + '/' + build_id[:2]
            target = target_parent + '/' + build_id[2:] + '.debug'
            if os.path.exists(target):
                continue
            symbol_file = symbol_index.lookup(build_id)
            if not symbol_file:
                print(f'Cannot find symbol file for {build_id}')
                continue
            print(f'Link {symbol_file} to {target}')
            os.makedirs(target_parent, exist_ok=True)
            os.link(symbol_file, target)


def main(argv):
    if len(argv) != 1:
        raise Exception('This script takes no argument')

    this_dir = os.path.dirname(os.path.abspath(argv[0]))
    target_dir = os.path.abspath(this_dir + '/../../../prebuilt/test_data/symbolizer/symbols')
    copy_symbols(this_dir, target_dir)


if __name__ == '__main__':
    main(sys.argv)
