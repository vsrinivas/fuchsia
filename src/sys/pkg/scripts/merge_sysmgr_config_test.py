#!/usr/bin/env python3.8

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import copy
import unittest

import merge_sysmgr_config


class TestMerge(unittest.TestCase):

    def test_merge_list(self):
        ls = ["foo"]

        merge_sysmgr_config.merge_list(ls, ["bar"])
        self.assertEqual(ls, ["foo", "bar"])

        with self.assertRaises(merge_sysmgr_config.DuplicateEntryError):
            merge_sysmgr_config.merge_list(ls, ["bar"])

    def test_merge_dict(self):
        d = {"foo": 1}

        merge_sysmgr_config.merge_dict(d, {"bar": 2})
        self.assertEqual(d, {"foo": 1, "bar": 2})

        with self.assertRaises(merge_sysmgr_config.DuplicateEntryError):
            merge_sysmgr_config.merge_dict(d, {"bar": 2})

    def test_nonrecursive(self):
        d = {"outer": {"inner": "value"}}

        with self.assertRaises(merge_sysmgr_config.DuplicateEntryError):
            merge_sysmgr_config.merge_dict(d, d)

    def test_merge_config(self):
        a = {
            "services": {
                "fuchsia.foo": "foo"
            },
            "update_dependencies": ["fuchsia.foo"],
            "exclusive": ["pass-through"]
        }

        b = {
            "services": {
                "fuchsia.bar": "bar"
            },
            "update_dependencies": ["fuchsia.bar"]
        }

        merged = copy.deepcopy(a)
        merge_sysmgr_config.merge_config(merged, b)
        self.assertEqual(
            merged, {
                "services": {
                    "fuchsia.bar": "bar",
                    "fuchsia.foo": "foo"
                },
                "update_dependencies": ["fuchsia.foo", "fuchsia.bar"],
                "exclusive": ["pass-through"]
            })

        merge_sysmgr_config.merge_config(b, a)
        self.assertEqual(
            b, {
                "services": {
                    "fuchsia.bar": "bar",
                    "fuchsia.foo": "foo"
                },
                "update_dependencies": ["fuchsia.bar", "fuchsia.foo"],
                "exclusive": ["pass-through"]
            })
