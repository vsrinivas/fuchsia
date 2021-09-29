#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys
import unittest
from unittest import mock
from parameterized import parameterized

import fidl_api_mapper

parser = argparse.ArgumentParser()
parser.add_argument(
    '--test_dir_path', help='Path to the test data directory.', required=True)
args = parser.parse_args()

TEST_DIR_PATH = args.test_dir_path
TEST_FILE_NAME = 'fidl.h'

# The python_host_test build rule calls `unittest.main`.
# So we need to get rid of the test arguments in order
# to prevent them from interfering with `unittest`'s args.
#
# Pop twice to get rid of the `--test_dir_path` flag and
# its value.
sys.argv.pop()
sys.argv.pop()


class DwarfdumpStreamingParserTest(unittest.TestCase):

    @parameterized.expand(
        [
            (
                'not_subprogram_skipped', [
                    '0x01:         DW_TAG_member',
                    '  DW_AT_name   ("member_name")',
                    '  DW_AT_decl_line   (1)',
                    '',
                ], {}),
            (
                'one_subprogram', [
                    '0x01:         DW_TAG_subprogram',
                    '  DW_AT_name   ("function_name")',
                    '  DW_AT_decl_line   (1)',
                    '',
                ], {
                    '0x01':
                        {
                            'DW_AT_name': '"function_name"',
                            'DW_AT_decl_line': '1'
                        }
                }),
            (
                'two_subprograms_sandwiching_non_subprogram', [
                    '0x01:         DW_TAG_subprogram',
                    '  DW_AT_name   ("function_name_1")',
                    '  DW_AT_decl_line   (1)',
                    '',
                    '0x99:         DW_TAG_member',
                    '  DW_AT_name   ("member_name")',
                    '  DW_AT_decl_line   (1)',
                    '',
                    '0x02:         DW_TAG_subprogram',
                    '  DW_AT_name   ("function_name_2")',
                    '  DW_AT_decl_line   (2)',
                    '',
                ], {
                    '0x01':
                        {
                            'DW_AT_name': '"function_name_1"',
                            'DW_AT_decl_line': '1'
                        },
                    '0x02':
                        {
                            'DW_AT_name': '"function_name_2"',
                            'DW_AT_decl_line': '2'
                        }
                }),
        ])
    def test_parse_line(self, name, dwarfdump_lines, expected_subprograms_dict):
        parser = fidl_api_mapper.DwarfdumpStreamingParser()
        for line in dwarfdump_lines:
            parser.parse_line(line)
        self.assertEqual(parser.get_subprograms(), expected_subprograms_dict)


class FidlApiResolverTest(unittest.TestCase):

    TEST_DATA_PATH = 'unknown'

    FIDL_HEADER_FILE_CONTENT = '\n'.join(
        [
            '// cts-coverage-fidl-name:line_2',
            'first_method()',
            '// cts-coverage-fidl-name:line_4',
            'middle_method()',
            'non_fidl_method()',
            '// cts-coverage-fidl-name:line_7',
            'last_method()',
        ])

    @parameterized.expand(
        [
            ('empty_subprograms_dict', {}, {}, {}),
            (
                'successfully_resolved_first_line', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '2',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {
                    'mangled_name': 'line_2'
                }),
            (
                'successfully_resolved_middle_line', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '4',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {
                    'mangled_name': 'line_4'
                }),
            (
                'successfully_resolved_last_line', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '7',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {
                    'mangled_name': 'line_7'
                }),
            (
                'non_annotated_function_is_skipped', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '5',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {}),
            (
                'line_number_too_large', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '100',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {}),
            (
                'subprogram_missing_filepath', {
                    '0x1':
                        {
                            'DW_AT_decl_line': '1',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {}),
            (
                'subprogram_missing_line_number', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {}),
            (
                'subprogram_is_not_in_fidl_header', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.cc"',
                            'DW_AT_decl_line': '1',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {}),
            (
                'subprogram_missing_mangled_name', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '1',
                        }
                }, {}, {}),
            (
                'subprogram_already_in_mapping', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '1',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {
                    'mangled_name': 'fidl_api_name'
                }, {
                    'mangled_name': 'fidl_api_name'
                }),
        ])
    def test_add_new_mappings(
            self, name, subprograms_dict, input_mapping_dict,
            expect_mapping_dict):
        resolver = fidl_api_mapper.FidlApiResolver(
            subprograms_dict, input_mapping_dict)
        with mock.patch(
                'builtins.open',
                mock.mock_open(read_data=self.FIDL_HEADER_FILE_CONTENT)):
            resolver.add_new_mappings()
        self.assertEqual(input_mapping_dict, expect_mapping_dict)

    def test_add_new_mappings_snapshot_fidl_file(self):
        # This testcase differs from "test_add_new_mappings" in that it uses
        # a real generated FIDL binding header file snapshotted at a particular
        # revision. So there's no need to mock the content of the file and we
        # can just directly read it instead.
        testdata_path = os.path.join(TEST_DIR_PATH, 'snapshot', TEST_FILE_NAME)
        subprograms_dict = {
            '0x1':
                {
                    'DW_AT_decl_file': '"%s"' % testdata_path,
                    'DW_AT_decl_line': '671',
                    'DW_AT_linkage_name': '"mangled_name"'
                }
        }
        expect_mapping_dict = {
            'mangled_name': 'fuchsia.diagnostics.stream/Source.RetireBuffer'
        }
        mapping_dict = {}

        resolver = fidl_api_mapper.FidlApiResolver(
            subprograms_dict, mapping_dict)
        resolver.add_new_mappings()
        self.assertEqual(mapping_dict, expect_mapping_dict)

    def test_add_new_mappings_generated_fidl_file(self):
        # Test against generated fidl headers from the current build.
        # Since the line of function declaration can change, we can just
        # the following assertions:
        #    1) At least 1 mapping can be added from iterating through all
        #       lines in the generated fidl header.
        #    2) No crashes from iterating all lines.
        testdata_path = os.path.join(TEST_DIR_PATH, 'generated', TEST_FILE_NAME)
        num_lines = len(open(testdata_path).readlines())

        subprograms_dict = {}
        mapping_dict = {}
        for line_num in range(1, num_lines + 1):
            subprograms_dict = {
                '0x1':
                    {
                        'DW_AT_decl_file': '"%s"' % testdata_path,
                        'DW_AT_decl_line': '%d' % line_num,
                        'DW_AT_linkage_name': '"mangled_name_%d"' % line_num
                    }
            }
            resolver = fidl_api_mapper.FidlApiResolver(
                subprograms_dict, mapping_dict)
            resolver.add_new_mappings()
        self.assertTrue(mapping_dict)
