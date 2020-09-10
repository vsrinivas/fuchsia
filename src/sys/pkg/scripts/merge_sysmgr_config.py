#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Merge sysmgr config files

This module defines a command line tool to merge multiple distinct sysmgr config
files into a single config, verifying that no files define duplicate entries or
duplicate keys with different values.  As sysmgr config files consist of a
single JSON object with values that are arrays/dicts of strings only, the merge
operation is not recursive.
"""

import argparse
import copy
import json
import unittest


def parseargs():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--inputs", help="newline separated file of input files")
    parser.add_argument("--output", help="where to write the output")
    return parser.parse_args()


def main(args):
    configs = load_configs(args.inputs)
    merged = merge_configs(configs)

    with open(args.output, 'w') as f:
        json.dump(merged, f, sort_keys=True, indent=2)


def load_configs(config_list_path):
    res = []
    with open(config_list_path) as config_list:
        for config_path in config_list:
            with open(config_path.rstrip()) as f:
                res.append(json.load(f))
    return res


def merge_configs(configs):
    merged = {}
    for config in configs:
        merge_config(merged, config)
    return merged


def merge_config(merged, config):
    for key, value in config.items():
        if key in merged:
            merge_item(merged[key], value)
        else:
            merged[key] = value


def merge_item(target, rest):
    # sanity check types are consistent across configs
    if type(target) != type(rest):
        raise DifferentTypesError(target, rest)

    if isinstance(target, dict):
        merge_dict(target, rest)
    elif isinstance(target, list):
        merge_list(target, rest)
    else:
        raise Exception("unexpected item type: {0}".format(type(target)))


def merge_list(target, rest):
    """Shallow merge a list into another, bailing on duplicates"""
    for item in rest:
        # sanity check we aren't trying to duplicate entries
        if item in target:
            raise DuplicateEntryError(item, target)
        target.append(item)


def merge_dict(target, rest):
    """Shallow merge a dict into another, bailing on duplicate keys"""
    for key, value in rest.items():
        # sanity check we aren't trying to duplicate entries
        if key in target:
            raise DuplicateEntryError(key, target)
        target[key] = value


class DifferentTypesError(Exception):

    def __init__(self, a, b):
        super(DifferentTypesError, self).__init__(
            "expected same types, got {a!r} and {b!r}".format(a=a, b=b))


class DuplicateEntryError(Exception):

    def __init__(self, item, target):
        super(DuplicateEntryError, self).__init__(
            "{item!r} already in {target!r}".format(item=item, target=target))


class TestMerge(unittest.TestCase):
    """$ python2.7 -m unittest merge_sysmgr_config"""

    def test_merge_list(self):
        ls = ["foo"]

        merge_list(ls, ["bar"])
        self.assertEqual(ls, ["foo", "bar"])

        with self.assertRaises(DuplicateEntryError):
            merge_list(ls, ["bar"])

    def test_merge_dict(self):
        d = {"foo": 1}

        merge_dict(d, {"bar": 2})
        self.assertEqual(d, {"foo": 1, "bar": 2})

        with self.assertRaises(DuplicateEntryError):
            merge_dict(d, {"bar": 2})

    def test_nonrecursive(self):
        d = {"outer": {"inner": "value"}}

        with self.assertRaises(DuplicateEntryError):
            merge_dict(d, d)

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
        merge_config(merged, b)
        self.assertEqual(
            merged, {
                "services": {
                    "fuchsia.bar": "bar",
                    "fuchsia.foo": "foo"
                },
                "update_dependencies": ["fuchsia.foo", "fuchsia.bar"],
                "exclusive": ["pass-through"]
            })

        merge_config(b, a)
        self.assertEqual(
            b, {
                "services": {
                    "fuchsia.bar": "bar",
                    "fuchsia.foo": "foo"
                },
                "update_dependencies": ["fuchsia.bar", "fuchsia.foo"],
                "exclusive": ["pass-through"]
            })


if __name__ == "__main__":
    main(parseargs())
