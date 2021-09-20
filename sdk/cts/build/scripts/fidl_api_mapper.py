#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A helper script for generating a map of FIDL mangled C++ function names to fully qualified APIs.

The output of the script is used for correlating CTS C++ function coverage data
(keyed by mangled function names) to FIDL APIs.
"""

import argparse
import json
import os
import re
import subprocess
import sys


class DwarfdumpStreamingParser:
    """Helper class to parse streaming output from `llvm-dwarfdump`."""

    DWARF_TAG_MATCHER = re.compile(r'^(0x\S+):\s+(\S+)')
    SUBPROGRAM_ATTR_MATCHER = re.compile(r'^\s+(\S+)\s+\((.+)\)')
    SUBPROGRAM_TAG = 'DW_TAG_subprogram'

    def __init__(self):
        # Instance variables to track parser state from line to line.
        self._cur_addr = None
        self._in_subprogram_block = False

        # Dict of compilation unit addresses to their respective attributes.
        self._subprograms_dict = {}

    def parse_line(self, line):
        """Parses a line from `llvm-dwarfdump`'s output.

        Args:
          line (string): A line of `llvm-dwarfdump` output.
        """
        if self._in_subprogram_block:
            m = self.SUBPROGRAM_ATTR_MATCHER.search(line)
            if m:
                attr, value = m.group(1), m.group(2)
                self._subprograms_dict[self._cur_addr][attr] = value
            else:
                # Dwarfdump entries are separated by a newline.
                # So the first unmatched line after entering a subprogram block
                # marks the end of the subprogram block.
                self._in_subprogram_block = False
            return

        m = self.DWARF_TAG_MATCHER.search(line)
        if m:
            addr, tag = m.group(1), m.group(2)
            if tag == self.SUBPROGRAM_TAG:
                self._in_subprogram_block = True
                self._cur_addr = addr
                self._subprograms_dict[self._cur_addr] = {}

    def get_subprograms(self):
        """Returns the subprograms that have been successfully parsed.

        Returns:
          A dict(string,dict) containing subprogram addresses and respective dwarfdump attributes.
        """
        return self._subprograms_dict


class FidlApiResolver:
    """Helper class to resolve mangled C++ function names to corresponding FIDL APIs.

    For each mangled C++ function that's associated with a `fidl.h` file,
    we use information provided in dwarfdump's output to navigate to where the function
    is declared in the generated source file (FIDL binding header file). Once there, we
    can simply read the Fully-Qualified FIDL API name annotation that's set a fixed number
    of lines above where the function is declared.

    Args:
      subprograms_dict (dict(string,dict)): Dict containing subprogram addresses and
        dwarfdump attributes.
      api_mapping_dict (dict(string,string)): Dict containing mapping between mangled
        function names and fully qualified FIDL names.
    """

    def __init__(self, subprograms_dict, api_mapping_dict):
        self._subprograms_dict = subprograms_dict
        self._api_mapping_dict = api_mapping_dict

    def add_new_mappings(self):
        """Add new mangled_name to FIDL API mapping if it doesn't already exist.

        New mappings are added to `self._api_mapping_dict`.
        """
        for _, info in self._subprograms_dict.items():
            # Only care about subprograms with file and line number information.
            if 'DW_AT_decl_file' not in info or 'DW_AT_decl_line' not in info:
                continue

            # Only process FIDL binding headers.
            if not info['DW_AT_decl_file'].endswith('fidl.h"'):
                continue

            mangled_name = info.get('DW_AT_linkage_name') or info.get(
                'DW_AT_name')
            if not mangled_name:
                # Ignore subprograms with no names.
                continue

            sanitized_mangled_name = mangled_name.strip('"')
            if sanitized_mangled_name not in self._api_mapping_dict:
                sanitized_filepath = info['DW_AT_decl_file'].strip('"')
                line_num = int(info['DW_AT_decl_line'])
                self._add_mapping_entry(
                    sanitized_mangled_name, sanitized_filepath, line_num)

    def _add_mapping_entry(self, mangled_name, filepath, line_num):
        """Resolve mangled_name to FIDL API mapping and add as mapping entry.

        Resolve mapping by opening the file where the function is defined, and reading
        an annotation that's a fixed-number of lines above the line of function declaration.

        Args:
          mangled_name (string): Mangled C++ function name to map to a FIDL API.
          filepath (string): Path to generated FIDL binding file that declares the function related
            to the `mangled_name`.
          line_num (int): The line number in the FIDL binding file where the function is declared.
        """
        fidl_api_name = ''
        # TODO(chok): Read N lines before `line_num` when FQ FIDL name annotation is added.
        annotation_offset = 0
        cur_line = 0
        with open(filepath) as f:
            while cur_line != line_num - annotation_offset:
                fidl_api_name = f.readline().strip()
                cur_line += 1
        self._api_mapping_dict[mangled_name] = fidl_api_name

    def get_mapping(self):
        """Returns the mangled-name-to-FIDL-API mapping.

        Returns:
          A dict(str,str) containing mangled function names and their corresponding fully qualified
            FIDL API names.
        """
        return self._mapping


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--input',
        help=
        'File that contains filepaths to unstripped libraries & executables relative to the build dir.',
        required=True)
    parser.add_argument(
        '--output',
        help='Path to the output file containing FIDL to mangled name mapping.',
        required=True)
    parser.add_argument(
        '--depfile', help='Path to the depfile generated for GN.', required=True)
    parser.add_argument(
        '--dwarfdump', help='Path to `llvm-dwarfdump` executable.', required=True)
    args = parser.parse_args()

    depfile_inputs = []
    with open(args.input) as f:
        depfile_inputs = f.read().splitlines()

    # Write depfile.
    with open(args.depfile, 'w') as f:
        f.write('%s: %s' % (args.output, ' '.join(sorted(depfile_inputs))))

    # Generate mapping.
    fidl_mangled_name_to_api_mapping = {}
    for unstripped_binary in depfile_inputs:
        dwarddump_cmd = [args.dwarfdump, unstripped_binary]
        parser = DwarfdumpStreamingParser()
        # TODO(chok): Add multithreaded support for concurrently processing each unstripped binary.
        with subprocess.Popen(dwarddump_cmd, stdout=subprocess.PIPE) as p:
            for line in p.stdout:
                parser.parse_line(line.decode())
        resolver = FidlApiResolver(
            parser.get_subprograms(), fidl_mangled_name_to_api_mapping)
        resolver.add_new_mappings()

    # Write output.
    with open(args.output, 'w') as f:
        f.write(json.dumps(fidl_mangled_name_to_api_mapping, indent=2))


if __name__ == '__main__':
    sys.exit(main())
