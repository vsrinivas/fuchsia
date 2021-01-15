#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock

import action_tracer


class CheckAccessLineTests(unittest.TestCase):

    def test_allowed_read(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "r", "foo.txt", allowed_reads={"foo.txt"}, allowed_writes={}),
            [])

    def test_forbiddden_read(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "r", "bar.txt", allowed_reads={}, allowed_writes={}),
            [("read", "bar.txt")])

    def test_allowed_write(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "w", "foo.txt", allowed_reads={}, allowed_writes={"foo.txt"}),
            [])

    def test_forbiddden_write(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "w", "baz.txt", allowed_reads={}, allowed_writes={}),
            [("write", "baz.txt")])

    def test_allowed_touch(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "t", "foo.txt", allowed_reads={}, allowed_writes={"foo.txt"}),
            [])

    def test_forbiddden_touch(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "t", "baz.txt", allowed_reads={}, allowed_writes={}),
            [("write", "baz.txt")])

    def test_allowed_delete(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "d", "foo.txt", allowed_reads={}, allowed_writes={"foo.txt"}),
            [])

    def test_forbiddden_delete(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "d", "baz.txt", allowed_reads={}, allowed_writes={}),
            [("write", "baz.txt")])

    def test_allowed_move(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m",
                "dest|source",
                allowed_reads={},
                allowed_writes={"source", "dest"}), [])

    def test_forbidden_move_from_source(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m", "dest|source", allowed_reads={}, allowed_writes={"dest"}),
            [("write", "source")])

    def test_forbidden_move_to_dest(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m", "dest|source", allowed_reads={},
                allowed_writes={"source"}), [("write", "dest")])

    def test_forbidden_move_source_and_dest(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m", "dest|source", allowed_reads={}, allowed_writes={}),
            [("write", "dest"), ("write", "source")])


if __name__ == '__main__':
    unittest.main()
