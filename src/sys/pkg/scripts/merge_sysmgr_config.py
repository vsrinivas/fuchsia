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


def parseargs():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--inputs_file",
        type=argparse.FileType('r'),
        help="newline separated file of input files",
    )
    parser.add_argument(
        "--output_file",
        type=argparse.FileType('w'),
        help="where to write the output",
    )
    parser.add_argument(
        '--depfile',
        type=argparse.FileType('w'),
        required=True,
    )
    return parser.parse_args()


def main(args):
    configs, files_read = load_configs(args.inputs_file)
    args.depfile.write(
        "{}: {}\n".format(args.output_file.name, " ".join(files_read)))

    merged = merge_configs(configs)
    json.dump(merged, args.output_file, sort_keys=True, indent=2)


def load_configs(config_list_file):
    res = []
    config_files = []
    for config_path in config_list_file:
        p = config_path.rstrip()
        config_files.append(p)
        with open(p) as f:
            res.append(json.load(f))
    return res, config_files


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


if __name__ == "__main__":
    main(parseargs())
