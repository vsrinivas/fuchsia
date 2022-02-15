#!/usr/bin/python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import unittest
from sdk_element_adapter import Adapter


class TestVersionHistory(unittest.TestCase):

    def setUp(self):
        self.maxDiff = None
        element_meta = json.loads(
            """{
            "data": {
                "name": "Platform version map",
                "type": "version_history",
                "versions": [
                    {
                        "abi_revision": "0x1FA3D8DDFBEDC6C7",
                        "api_level": "1"
                    }
                ]
            },
            "schema_id": "https://fuchsia.dev/schema/version_history-ef02ef45.json"
        }""")
        gn_meta = json.loads(
            """[
            {
                "files": [],
                "meta": {
                    "dest": "version_history.json",
                    "source": "//out/default/gen/sdk/version_history_sdk_element_sdk_metadata.json"
                }
            }
        ]""")
        gn_label = '//sdk:version_history(//build/toolchain/fuchsia:x64)'
        base_out_dir = '//out/default'
        category = 'public'
        atom_meta_path = 'gen/sdk/version_history_adapter.meta.json'
        self.adapter = Adapter(
            element_meta, gn_meta[0], gn_label, base_out_dir, category,
            atom_meta_path).adapt()

    def test_meta(self):
        expected = {
            'data':
                {
                    'name':
                        'Platform version map',
                    'type':
                        'version_history',
                    'versions':
                        [{
                            'abi_revision': '0x1FA3D8DDFBEDC6C7',
                            'api_level': '1'
                        }],
                },
            'schema_id':
                'https://fuchsia.dev/schema/version_history-ef02ef45.json'
        }
        self.assertEqual(self.adapter['meta'], expected)

    def test_manifest(self):
        expected = {
            'atoms':
                [
                    {
                        'category':
                            'public',
                        'deps': [],
                        'files':
                            [
                                {
                                    'destination':
                                        'version_history.json',
                                    'source':
                                        'gen/sdk/version_history_adapter.meta.json'
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
        self.assertEqual(self.adapter['manifest'], expected)


class TestHostTool(unittest.TestCase):

    def setUp(self):
        self.maxDiff = None
        element_meta = json.loads(
            """{
            "data":
                {
                    "contents": {
                        "binary": "cmc"
                    },
                    "description": "cmc processes component manifests",
                    "host_arch": "x64",
                    "host_os": "linux",
                    "name": "cmc",
                    "element_type": "host_tool"
                },
            "schema_id":
                "https://fuchsia.dev/schema/sdk/host_tool_sdk_element-00000000.json"
        }""")
        gn_meta = json.loads(
            """[
            {
                "files": [
                    {
                        "dest": "cmc",
                        "source": "//out/default/host_x64/cmc"
                    }
                ],
                "meta": {
                    "dest": "cmc_sdk_element_sdk_metadata.json",
                    "source": "//out/default/host_x64/gen/tools/cmc/cmc_sdk_element_sdk_metadata.json"
                }
            }
        ]""")
        gn_label = '//tools/cmc:cmc_sdk(//build/toolchain:host_x64)'
        base_out_dir = '//out/default'
        category = 'partner'
        atom_meta_path = 'host_x64/gen/cmc_adapter.meta.json'
        self.adapter = Adapter(
            element_meta, gn_meta[0], gn_label, base_out_dir, category,
            atom_meta_path).adapt()

    def test_meta(self):
        expected = {
            'files': ['tools/x64/cmc'],
            'name': 'cmc',
            'root': 'tools',
            'type': 'host_tool'
        }
        self.assertEqual(self.adapter['meta'], expected)

    def test_manifest(self):
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
                                        'tools/x64/cmc-meta.json',
                                    'source':
                                        'host_x64/gen/cmc_adapter.meta.json'
                                }
                            ],
                        'gn-label':
                            '//tools/cmc:cmc_sdk(//build/toolchain:host_x64)',
                        'id':
                            'sdk://tools/x64/cmc',
                        'meta':
                            'tools/x64/cmc-meta.json',
                        'plasa': [],
                        'type':
                            'host_tool'
                    }
                ],
            'ids': ['sdk://tools/x64/cmc']
        }
        self.assertEqual(self.adapter['manifest'], expected)


if __name__ == '__main__':
    unittest.main()
