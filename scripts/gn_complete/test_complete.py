#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import complete
import unittest

class TestLabelSplitting(unittest.TestCase):

  def test_absolute(self):
    self.assertEqual(complete.split_label('//'), ['//'])
    self.assertEqual(complete.split_label('//foo/bar'), ['//', 'foo', '/bar'])
    self.assertEqual(complete.split_label('//foo/bar:baz'),
                     ['//', 'foo', '/bar', ':baz'])
    self.assertEqual(complete.split_label('//:baz'), ['//', ':baz'])

  def test_relative(self):
    self.assertEqual(complete.split_label(''), [''])
    self.assertEqual(complete.split_label('foo/bar'), ['foo', '/bar'])
    self.assertEqual(complete.split_label('foo/bar:baz'),
                     ['foo', '/bar', ':baz'])
    self.assertEqual(complete.split_label('/foo/bar:baz'),
                     ['/foo', '/bar', ':baz'])
    self.assertEqual(complete.split_label(':baz'), [':baz'])

  def test_toolchain(self):
    self.assertEqual(complete.split_label('foo/bar:baz(//build:tool)'),
                     ['foo', '/bar', ':baz(//build:tool)'])
    self.assertEqual(complete.split_label('baz(//build:tool)'),
                     ['baz(//build:tool)'])
    self.assertEqual(complete.split_label('(//build:tool)'), ['(//build:tool)'])


class TestCompletions(unittest.TestCase):

  def test_absolute(self):
    completions = complete.Completions('//')
    completions.insert_label('//:test')
    completions.insert_label('//foo/bar:test')
    completions.insert_label('//foo/bar/baz:test')
    completions.insert_label('//foo/baz:test')
    self.assertListEqual(list(completions.list_completions()),
                          ['//:test', '//foo'])

  def test_relative_no_leading_slash(self):
    completions = complete.Completions('src/')
    completions.insert_label('src/foo/bar:test')
    completions.insert_label('src/foo/bar/baz:foo')
    completions.insert_label('src/foo/bar/bat:fee')
    self.assertListEqual(list(completions.list_completions()),
                          ['src/foo/bar:test', 'src/foo/bar/baz',
                           'src/foo/bar/bat'])

  def test_relative_leading_slash(self):
    completions = complete.Completions('src')
    completions.insert_label('src/foo/bar:test')
    completions.insert_label('src/foo/bar/baz:foo')
    completions.insert_label('src/foo/bar/bat:fee')
    self.assertListEqual(list(completions.list_completions()),
                          ['src/foo/bar:test', 'src/foo/bar/baz',
                           'src/foo/bar/bat'])

  def test_relative_at_root(self):
    completions = complete.Completions('')
    completions.insert_label(':test')
    completions.insert_label('/foo/bar:foo')
    self.assertListEqual(list(completions.list_completions()),
                          [':test', '/foo'])

  def test_long_common_prefix(self):
    completions = complete.Completions('')
    completions.insert_label('foo/bar/baz/boo:test')
    completions.insert_label('foo/bar/baz/boo:main')
    completions.insert_label('foo/bar/baz/boo:bin')
    self.assertListEqual(list(completions.list_completions()),
                          ['foo/bar/baz/boo:test', 'foo/bar/baz/boo:main',
                           'foo/bar/baz/boo:bin'])

  def test_exact_match_is_also_prefix(self):
    completions = complete.Completions('foo:bar')
    completions.insert_label('foo:bar')
    completions.insert_label('foo:bar_baz')
    self.assertListEqual(list(completions.list_completions()),
                          ['foo:bar', 'foo:bar_baz'])

  def test_empty(self):
    completions = complete.Completions('')
    self.assertListEqual(list(completions.list_completions()), [])
