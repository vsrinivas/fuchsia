#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Example usage:
$ fx set ...
$ scripts/gn/gen_visibility_globs.py \
     --all="//build/config/rust:deny_unused_results" \
     --allow="//build/config/rust:allow_unused_results"

The output is useful for instance if you have a visibility allowlist that you want to maintain with
shortened glob paths.

To run this script's self-tests:
$ TEST=1 scripts/gn/gen_visibility_globs.py
"""

import argparse
import sys
import gn_util
import unittest
import os


def main():
    parser = argparse.ArgumentParser(
        description="Prints a visibility list for gn configs")
    parser.add_argument(
        "--all",
        help="gn target that produces a universe of labels to consider",
        required=True)
    parser.add_argument(
        "--allow",
        help="gn target whose current users we want to produce an allowlist for",
        required=True)
    parser.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Print progress and stats")
    parser.add_argument(
        "--ignore-suffix",
        help="comma-separated list of label suffixes to ignore",
        default="")

    def verbose(*vargs, **kwargs):
        if args.verbose:
            print(*vargs, **kwargs)

    args = parser.parse_args()
    verbose("Getting all labels list...")
    all_labels = gn_util.gn_refs(args.all)
    verbose(f"Found {len(all_labels)} elements in universe.")
    ignore_suffix = args.ignore_suffix.split(",")
    all_labels = (
        label for label in all_labels
        if not any(label.endswith(suffix) for suffix in ignore_suffix))
    allow_labels = gn_util.gn_refs(args.allow)
    verbose(f"Found {len(allow_labels)} elements to allow.")
    tree = Node("/")
    update_tree(tree, all_labels, False)
    update_tree(tree, allow_labels, True)
    for path, include in tree.glob_collect():
        path = "/".join(path)
        if include:
            print(f"\"{path}\",")
        else:
            verbose(f"ignored {path}")

    return 0


class Node:

    def __init__(self, name, value=None):
        self.name = name
        self.children = []
        self.children = dict()
        self.value = value

    def ensure_child(self, name):
        if name not in self.children:
            self.children[name] = Node(name)
        return self.children[name]

    def visit(self, path=None):
        if path is None:
            path = []
        p = path + [self.name]
        if len(self.children) == 0:
            yield p
        for k, c in self.children.items():
            for d in c.visit(p):
                yield d

    def glob_collect(self, path=None):
        if path is None:
            path = []
        p = path + [self.name]
        if len(self.children) == 0:
            yield p, self.value
            return
        child_items = []
        can_glob = True
        for k, c in self.children.items():
            for v, include in c.glob_collect(p):
                child_items.append((v, include))
                can_glob = can_glob and include
        if can_glob:
            yield p + ["*"], True
        else:
            for v, i in child_items:
                yield v, i


def get_label_parts(label):
    """returns the parts of an absolute label as a list"""
    return label[2:].replace(":", "/").split("/")


def update_tree(tree, labels, value):
    """updates the tree for all the labels in `labels` to assume `value`."""
    for l in labels:
        parts = get_label_parts(l)
        n = tree
        for part in parts:
            n = n.ensure_child(part)
        n.value = value


class Test(unittest.TestCase):

    def test_label_parts(self):
        self.assertEqual(
            get_label_parts("//foo/bar:baz"), ["foo", "bar", "baz"])

    def test_build_tree(self):
        tree = Node("/")
        update_tree(tree, ["//foo/bar:baz", "//foo/bar:bar"], False)
        bar = tree.children["foo"].children["bar"]
        self.assertIsNone(bar.value)
        self.assertFalse(bar.children["bar"].value)
        self.assertFalse(bar.children["baz"].value)
        items = []
        for x in tree.visit():
            items.append("/".join(x))
        items.sort()
        self.assertEqual(items, ["//foo/bar/bar", "//foo/bar/baz"])

        update_tree(tree, ["//foo/bar:baz"], True)
        self.assertFalse(bar.children["bar"].value)
        self.assertTrue(bar.children["baz"].value)

    def test_glob_collect(self):
        tree = Node("/")
        update_tree(
            tree, ["//foo/bar:baz", "//foo/bar:bar", "//foo/apple:banana"],
            False)
        update_tree(tree, ["//foo/bar:baz", "//foo/bar:bar"], True)
        accepted = []
        denied = []
        for i, inc in tree.glob_collect():
            p = "/".join(i)
            if inc:
                accepted.append(p)
            else:
                denied.append(p)
        self.assertEqual(accepted, ["//foo/bar/*"])
        self.assertEqual(denied, ["//foo/apple/banana"])


if __name__ == "__main__":
    if os.getenv("TEST") is not None:
        unittest.main()
    else:
        sys.exit(main())
