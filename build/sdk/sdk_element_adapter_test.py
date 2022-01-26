#!/usr/bin/python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from sdk_element_adapter import Adapter


class TestVersionHistory(unittest.TestCase):

    def setUp(self):
        self.maxDiff = None
        element_meta = {
            'data':
                {
                    'name':
                        'Platform version map',
                    'element_type':
                        'version_history',
                    'versions':
                        [{
                            'abi_revision': '0x02160C9D',
                            'api_level': '1'
                        }],
                },
            'schema_id':
                'https://fuchsia.dev/schema/version_history-038fa854.json'
        }
        element_manifest = [
            {
                'dst': 'version_history.json',
                'src': 'gen/sdk/version_history_sdk_element_sdk_metadata.json'
            },
        ]
        gn_label = '//sdk:version_history(//build/toolchain/fuchsia:x64)'
        meta_out = 'gen/sdk/version_history.meta.json'
        self.adapter = Adapter(
            element_meta, element_manifest, gn_label, meta_out)

    def test_atom_meta(self):
        expected = {
            'data':
                {
                    'name':
                        'Platform version map',
                    'element_type':
                        'version_history',
                    'versions':
                        [{
                            'abi_revision': '0x02160C9D',
                            'api_level': '1'
                        }],
                },
            'schema_id':
                'https://fuchsia.dev/schema/version_history-038fa854.json'
        }
        self.assertEqual(self.adapter.atom_meta(), expected)

    def test_atom_manifest(self):
        expected = {
            'atoms':
                [
                    {
                        'category':
                            'partner',
                        'deps': [],
                        'files':
                            [
                                {
                                    'destination':
                                        'version_history.json',
                                    'source':
                                        'gen/sdk/version_history.meta.json'
                                }
                            ],
                        'gn-label':
                            '//sdk:version_history(//build/toolchain/fuchsia:x64)',
                        'id':
                            'sdk://version_history',
                        'meta':
                            'version_history.json',
                        'plasa': [],
                        'type':
                            'version_history'
                    }
                ],
            'ids': ['sdk://version_history']
        }
        self.assertEqual(self.adapter.atom_manifest(), expected)


class TestHostTool(unittest.TestCase):

    def setUp(self):
        self.maxDiff = None
        element_meta = {
            'data':
                {
                    'contents': {
                        'binary': 'cmc'
                    },
                    'description': 'cmc processes component manifests',
                    'host_arch': 'x64',
                    'host_os': 'linux',
                    'name': 'cmc',
                    'element_type': 'host_tool'
                },
            'schema_id':
                'https://fuchsia.dev/schema/sdk/host_tool_sdk_element-00000000.json'
        }
        element_manifest = [
            {
                'dst': 'cmc',
                'src': 'host_x64/cmc'
            }, {
                'dst':
                    'cmc_sdk_element_sdk_metadata.json',
                'src':
                    'host_x64/gen/tools/cmc/cmc_sdk_element_sdk_metadata.json'
            }
        ]
        gn_label = '//tools/cmc:cmc_sdk(//build/toolchain:host_x64)'
        meta_out = 'host_x64/gen/tools/cmc/cmc_sdk_element_adapter.meta.json'
        self.adapter = Adapter(
            element_meta, element_manifest, gn_label, meta_out)

    def test_atom_meta(self):
        expected = {
            'files': ['tools/x64/cmc'],
            'name': 'cmc',
            'root': 'tools',
            'type': 'host_tool'
        }
        self.assertEqual(self.adapter.atom_meta(), expected)

    def test_atom_manifest(self):
        expected = {
            'atoms':
                [
                    {
                        'category':
                            'partner',
                        'deps': [],
                        'files':
                            [
                                {
                                    'destination': 'tools/x64/cmc',
                                    'source': 'host_x64/cmc'
                                }, {
                                    'destination':
                                        'tools/x64/cmc_sdk_element_sdk_metadata.json',
                                    'source':
                                        'host_x64/gen/tools/cmc/cmc_sdk_element_adapter.meta.json'
                                }
                            ],
                        'gn-label':
                            '//tools/cmc:cmc_sdk(//build/toolchain:host_x64)',
                        'id':
                            'sdk://tools/x64/cmc',
                        'meta':
                            'tools/x64/cmc_sdk_element_sdk_metadata.json',
                        'plasa': [],
                        'type':
                            'host_tool'
                    }
                ],
            'ids': ['sdk://tools/x64/cmc']
        }
        self.assertEqual(self.adapter.atom_manifest(), expected)


if __name__ == '__main__':
    unittest.main()
