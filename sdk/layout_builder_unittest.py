#!/usr/bin/python3.5
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(INTK-247): switch to the standard shebang line when the mocking library
# is available.

import unittest
from unittest.mock import patch, MagicMock

from layout_builder import Builder, _process_manifest_data


def _atom(domain, name):
    return {
        'id': {
          'domain': domain,
          'name': name,
        },
        'gn-label': '//foo/bar:blah',
        'package-deps': [],
        'deps': [],
        'files': [],
        'tags': [],
    }


def _manifest(atoms):
    return {
        'atoms': atoms,
        'ids': [],
        'meta': {
            'host-arch': 'fuchsia',
            'target-arch': 'fuchsia-too',
        },
    }


class LayoutBuilderTests(unittest.TestCase):

    @patch('layout_builder.Builder')
    def test_different_domains(self, builder):
        builder.domains = ['cpp']
        manifest = _manifest([_atom('exe', 'foo')])
        self.assertFalse(_process_manifest_data(manifest, builder))
        builder.prepare.assert_not_called()
        builder.finalize.assert_not_called()

    @patch('layout_builder.Builder')
    def test_install_one(self, builder):
        builder.install_exe_atom = MagicMock()
        builder.domains = ['exe']
        manifest = _manifest([_atom('exe', 'foo')])
        self.assertTrue(_process_manifest_data(manifest, builder))
        self.assertTrue(builder.prepare.called)
        self.assertEqual(builder.install_exe_atom.call_count, 1)
        self.assertTrue(builder.finalize.called)

    @patch('layout_builder.Builder')
    def test_install_two(self, builder):
        builder.install_exe_atom = MagicMock()
        builder.domains = ['exe']
        manifest = _manifest([_atom('exe', 'foo'), _atom('exe', 'bar')])
        self.assertTrue(_process_manifest_data(manifest, builder))
        self.assertTrue(builder.prepare.called)
        self.assertEqual(builder.install_exe_atom.call_count, 2)
        self.assertTrue(builder.finalize.called)

    @patch('layout_builder.Builder')
    def test_install_two_different(self, builder):
        builder.install_exe_atom = MagicMock()
        builder.install_c_pp_atom = MagicMock()
        builder.domains = ['cpp', 'exe']
        manifest = _manifest([_atom('exe', 'foo'), _atom('cpp', 'bar')])
        self.assertTrue(_process_manifest_data(manifest, builder))
        self.assertTrue(builder.prepare.called)
        self.assertEqual(builder.install_cpp_atom.call_count, 1)
        self.assertEqual(builder.install_exe_atom.call_count, 1)
        self.assertTrue(builder.finalize.called)


if __name__ == '__main__':
    unittest.main()
