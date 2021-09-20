#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
from unittest import mock
from parameterized import parameterized

import fidl_api_mapper


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

    @parameterized.expand(
        [
            ('empty_subprograms_dict', {}, {}, {}),
            (
                'successfully_resolved_first_line', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '1',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {
                    'mangled_name': 'first'
                }),
            (
                'successfully_resolved_middle_line', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '2',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {
                    'mangled_name': 'middle'
                }),
            (
                'successfully_resolved_last_line', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '3',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {
                    'mangled_name': 'last'
                }),
            (
                'line_number_too_large', {
                    '0x1':
                        {
                            'DW_AT_decl_file': '"fidl.h"',
                            'DW_AT_decl_line': '100',
                            'DW_AT_linkage_name': '"mangled_name"'
                        }
                }, {}, {
                    'mangled_name': ''
                }),
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
        fidl_header_file_content = 'first\nmiddle\nlast'
        resolver = fidl_api_mapper.FidlApiResolver(
            subprograms_dict, input_mapping_dict)
        with mock.patch('builtins.open',
                        mock.mock_open(read_data=fidl_header_file_content)):
            resolver.add_new_mappings()
        self.assertEqual(input_mapping_dict, expect_mapping_dict)
