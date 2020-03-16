#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import frontend
import os
import tempfile
import unittest


class TestFrontend(unittest.TestCase):

    def test_load_metadata_success(self):
        tf = tempfile.NamedTemporaryFile()
        tf.write(
            '{"parts": [{"meta": "tools/x64/zbi-meta.json", '
            '"type": "host_tool"}, {"meta": "tools/x64/zxdb-meta.json", '
            '"type": "host_tool"}, {"meta": "tools/zbi-meta.json", '
            '"type": "host_tool"}], "arch": {"host": "x86_64-linux-gn", '
            '"target": ["arm64", "x64"]}, "id": "0.20200313.2.1", '
            '"schema_version": "1"}')
        tf.flush()
        metadata = frontend.load_metadata(tf.name)
        self.assertEqual(3, len(metadata['parts']))
        self.assertEqual('x86_64-linux-gn', metadata['arch']['host'])
        self.assertEqual('0.20200313.2.1', metadata['id'])

    def test_load_metadata_fail(self):
        tf = tempfile.NamedTemporaryFile()
        tf.write('invalid json')
        self.assertFalse(frontend.load_metadata(tf.name))
        tf.close()  # temp file is removed when closed
        self.assertFalse(frontend.load_metadata(tf.name))


if __name__ == '__main__':
    unittest.main()
