# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re
import itertools
import unittest
import json
import subprocess
import functools

# Group 1: directory
# Group 2 (optional): target in dir
TARGET_EXP = re.compile(r"\/\/([\w\-_]*(?:\/[\w\-_]+)*)(?::([\w\-u]*))?")


def gn_refs(target):
    """Invokes fx gn refs [target] returning the references as a list"""
    return run_command(["fx", "gn", "refs",
                        get_out_dir(), target]).strip().splitlines()


def gn_desc(target, attributes):
    if attributes is not list:
        attributes = [attributes]
    """Invokes fx gn desc [target] [attributes...] returning each line as a list element"""
    return run_command(
        ["fx", "gn", "desc", get_out_dir(), target] +
        attributes).strip().splitlines()


def run_command(command):
    """Runs command and returns stdout output"""
    return subprocess.check_output(
        command, stderr=subprocess.STDOUT, encoding="utf8")


# functools.cache is more semantically accurate, but requires Python 3.9
@functools.lru_cache
def get_out_dir():
    """Retrieve the build output directory"""

    fx_status = run_command(["fx", "status", "--format=json"])
    return json.loads(
        fx_status)["environmentInfo"]["items"]["build_dir"]["value"]


# TODO(shayba): consider supporting local labels (not just absolutes)
def target_to_dir(target):
    """Returns likely directory path for a target's BUILD.gn file."""
    m = TARGET_EXP.match(target)
    return m.group(1)


def canonicalize_target(target):
    """Turns "//foo/bar:bar" into "//foo/bar"."""
    m = TARGET_EXP.match(target)
    directory = m.group(1)
    inner_target = m.group(2)
    if inner_target and inner_target == directory.rpartition("/")[2]:
        return "//" + directory
    else:
        return target


# TODO(shayba): consider supporting local labels (not just absolutes)
def is_visible_to(target, visibility):
    """Returns whether dst_target is visible to src_target."""
    target_dirs, _, target_label = target.partition(":")
    visibility_dirs, _, visibility_label = visibility.partition(":")
    for target_dir, visibility_dir in itertools.zip_longest(
            target_dirs.split("/"), visibility_dirs.split("/")):
        if visibility_dir == "*":
            return True
        if target_dir != visibility_dir:
            return False
    if visibility_label == "*":
        return True
    return target_label == visibility_label


class Test(unittest.TestCase):

    def test_target_to_dir(self):

        def expect(target, build_dir):
            self.assertTrue(target_to_dir(target), build_dir)

        expect(r"//build/config:Wno-conversion", "build/config")
        expect(r"//foo/bar/*", "foo/bar")
        expect(r"//foo:bar", "foo/bar")

    def test_is_visible_to(self):

        def is_visible(src, dst):
            self.assertTrue(is_visible_to(src, dst))

        def not_visible(src, dst):
            self.assertFalse(is_visible_to(src, dst))

        is_visible("//foo:bar", "*")
        is_visible("//foo:bar", "//foo/*")
        is_visible("//foo:bar", "//foo:bar")
        is_visible("//foo:bar", "//foo:*")
        not_visible("//foo:bar", "//foo:baz")
        not_visible("//foo:bar", "//foo/baz")
        not_visible("//foo/bar:baz", "//foo/bar")


if __name__ == "__main__":
    unittest.main()
