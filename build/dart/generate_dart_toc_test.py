#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import yaml
import collections
from parameterized import parameterized
import generate_dart_toc
from unittest import mock


class GenDartTocTest(unittest.TestCase):

    def test_configure_yaml(self):

        generate_dart_toc.configure_yaml()
        self.assertIn(collections.OrderedDict, yaml.Dumper.yaml_representers)

    @parameterized.expand(
        [
            (
                'success_only_title', "t1", None, None,
                collections.OrderedDict({"title": "t1"})),
            (
                'success_all_fields', "t1", "p1", ["i1"],
                collections.OrderedDict(
                    {
                        "title": "t1",
                        "path": "p1",
                        "section": ["i1"]
                    })),
        ])
    def test_create_toc_item(
            self, name, title, path, subitems, expected_result):
        result = generate_dart_toc.build_toc_item(title, path, subitems)
        self.assertEqual(result, expected_result)

    @parameterized.expand(
        [
            (
                'success_single', [
                    {
                        "qualifiedName": "a",
                        "packageName": "foo",
                        "type": "library"
                    }
                ], [
                    {
                        "qualifiedName": "a",
                        "packageName": "foo",
                        "type": "library",
                        "members": []
                    }
                ]),
            (
                'success_multiple_unrelated', [
                    {
                        "qualifiedName": "a",
                        "packageName": "foo",
                        "type": "library"
                    }, {
                        "qualifiedName": "b",
                        "packageName": "foo",
                        "type": "library"
                    }
                ], [
                    {
                        "qualifiedName": "a",
                        "packageName": "foo",
                        "type": "library",
                        "members": []
                    }, {
                        "qualifiedName": "b",
                        "packageName": "foo",
                        "type": "library",
                        "members": []
                    }
                ]),
            (
                'success_multiple_nested', [
                    {
                        'qualifiedName': 'a',
                        "packageName": "foo",
                        'type': 'library'
                    }, {
                        'qualifiedName': 'a.A',
                        "packageName": "foo",
                        'type': 'class',
                        'enclosedBy': {
                            'name': 'a',
                            'type': 'library'
                        }
                    }, {
                        'qualifiedName': 'a.A.==',
                        "packageName": "foo",
                        'type': 'method',
                        'enclosedBy': {
                            'name': 'A',
                            'type': 'class'
                        }
                    }
                ], [
                    {
                        'qualifiedName':
                            'a',
                        "packageName":
                            "foo",
                        'type':
                            'library',
                        'members':
                            [
                                {
                                    'qualifiedName':
                                        'a.A',
                                    "packageName":
                                        "foo",
                                    'type':
                                        'class',
                                    'enclosedBy':
                                        {
                                            'name': 'a',
                                            'type': 'library'
                                        },
                                    'members':
                                        [
                                            {
                                                'qualifiedName': 'a.A.==',
                                                "packageName": "foo",
                                                'type': 'method',
                                                'enclosedBy':
                                                    {
                                                        'name': 'A',
                                                        'type': 'class'
                                                    }
                                            }
                                        ]
                                }
                            ]
                    }
                ]),
            (
                'success_same_qualifiedName_diffpackage', [
                    {
                        "qualifiedName": "a",
                        "packageName": "foo",
                        "type": "library"
                    }, {
                        "qualifiedName": "a",
                        "packageName": "bar",
                        "type": "library"
                    }, {
                        'qualifiedName': 'a.A',
                        "packageName": "foo",
                        'type': 'class',
                        'enclosedBy': {
                            'name': 'a',
                            'type': 'library'
                        }
                    }
                ], [
                    {
                        'members':
                            [
                                {
                                    'enclosedBy':
                                        {
                                            'name': 'a',
                                            'type': 'library'
                                        },
                                    'members': [],
                                    'packageName': 'foo',
                                    'qualifiedName': 'a.A',
                                    'type': 'class'
                                }
                            ],
                        'packageName': 'foo',
                        'qualifiedName': 'a',
                        'type': 'library'
                    },
                    {
                        'members': [],
                        'packageName': 'bar',
                        'qualifiedName': 'a',
                        'type': 'library'
                    },
                ]), ('success_none', [{
                    "type": "not_in_enclosed"
                }], [])
        ])
    def test_treeify_event(self, name, data, expected_results):
        index = generate_dart_toc.treeify_index(data)
        self.assertEqual(index, expected_results)

    @parameterized.expand(
        [
            (
                "success_no_members", {
                    "name": "foo",
                    "href": "localhost"
                }, [
                    {
                        "title": "foo",
                        "path": "/reference/dart/localhost",
                        "sub_items": []
                    }
                ]),
            (
                "success_with_members", {
                    'type':
                        'library',
                    'name':
                        'a',
                    'href':
                        'a@local',
                    'members':
                        [
                            {
                                'type': 'class',
                                'name': 'A',
                                'href': 'A@local',
                                'enclosedBy':
                                    {
                                        'name': 'agent',
                                        'type': 'library'
                                    },
                                'members': []
                            }
                        ]
                }, [
                    {
                        "title": "A",
                        "path": "/reference/dart/A@local",
                        "sub_items": []
                    }, {
                        "title": "a",
                        "path": "/reference/dart/a@local",
                        "sub_items": [{
                            'heading': 'Classes'
                        }, {}]
                    }
                ])
        ])
    def test_element_to_toc_item(self, name, element, expected_calls):
        with mock.patch.object(generate_dart_toc, 'build_toc_item') as bld_itm:
            bld_itm.return_value = {}
            generate_dart_toc.element_to_toc_item(element)
            for call in expected_calls:
                bld_itm.assert_any_call(**call)

    def test_noargs_main(self):
        with mock.patch.object(generate_dart_toc, 'configure_yaml'):
            with mock.patch.object(generate_dart_toc,
                                   'build_toc_content') as bld_cnt:
                with mock.patch.object(generate_dart_toc.os.path,
                                       'isfile') as check_file:
                    with mock.patch.object(generate_dart_toc,
                                           'open') as mock_file:
                        with mock.patch.object(generate_dart_toc.yaml,
                                               'dump') as mock_yaml:
                            opened_file_mock = mock_file().__enter__()
                            check_file.return_value = True
                            fake_content = mock.MagicMock()
                            bld_cnt.return_value = fake_content
                            generate_dart_toc.no_args_main("index", "outfile")
                            mock_yaml.assert_called_with(
                                fake_content,
                                opened_file_mock,
                                default_flow_style=False)

    def test_noargs_main_no_index_file(self):
        with mock.patch.object(generate_dart_toc.os.path,
                               'isfile') as check_file:
            check_file.return_value = False
            with self.assertRaises(RuntimeError):
                generate_dart_toc.no_args_main("index", "outfile")

    def test_noargs_main_no_content(self):
        with mock.patch.object(generate_dart_toc, 'configure_yaml'):
            with mock.patch.object(generate_dart_toc,
                                   'build_toc_content') as bld_cnt:
                with mock.patch.object(generate_dart_toc.os.path,
                                       'isfile') as check_file:
                    check_file.return_value = True
                    bld_cnt.return_value = None
                    with self.assertRaises(RuntimeError):
                        generate_dart_toc.no_args_main("index", "outfile")

    @parameterized.expand(
        [
            ("no_library", [], {
                "toc": [{}]
            }),
            (
                "single_library_with_pkg",
                [{
                    "name": "foo",
                    "packageName": "bar"
                }], {
                    "toc": [{}, {
                        "heading": "bar"
                    }, []]
                }),
            (
                "single_library_no_pkg", [{
                    "name": "foo"
                }], {
                    "toc": [{}, {
                        "heading": "Libraries"
                    }, []]
                }),
            (
                "multi_library_with_pkg", [
                    {
                        "name": "test",
                        "packageName": "pack"
                    },
                    {
                        "name": "foo",
                        "packageName": "bar"
                    },
                ], {
                    "toc": [
                        {}, {
                            "heading": "bar"
                        }, [], {
                            "heading": "pack"
                        }, []
                    ]
                }),
            (
                "multi_library_no_pkg", [
                    {
                        "name": "test"
                    },
                    {
                        "name": "foo"
                    },
                ], {
                    "toc": [{}, {
                        "heading": "Libraries"
                    }, [], []]
                })
        ])
    def test_build_toc_content(self, name, libraries, expected_result):
        with mock.patch.object(generate_dart_toc, 'open'):
            with mock.patch.object(generate_dart_toc.json, 'load'):
                with mock.patch.object(generate_dart_toc,
                                       'treeify_index') as tr_ind:
                    with mock.patch.object(generate_dart_toc,
                                           'build_toc_item') as bld_toc:
                        with mock.patch.object(
                                generate_dart_toc,
                                'element_to_toc_item') as elem_to_toc:
                            tr_ind.return_value = libraries
                            bld_toc.return_value = {}
                            elem_to_toc.return_value = []
                            result = generate_dart_toc.build_toc_content("test")
                            self.assertEqual(expected_result, result)
