# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import base64
import gzip
import io

from far.far_reader import far_read, FarFormatError

INVALID_FILE = b"AAAAAAAAAAAAAAAAAA"
EOF_INDEX_LEN = b"\xc8\xbf\x0b\x48\xad\xab\xc5\x11\xAA"
EMPTY_ARCHIVE = b"\xc8\xbf\x0b\x48\xad\xab\xc5\x11\x00\x00\x00\x00\x00\x00\x00\x00"

CAT_FAR_GZ = """
H4sICFAerl0AA2NhdC5mYXIA7dBPSgMxGAXwrquC7l3IrLXN5P8IhQoKutCFnuBL8kUG26nYwY0I
3kc8gyvBk3gOW1uoui50836EkJeQR8jn+9b52+vHnugsnF5cH80Nl3m42r86uTy7eVnmg87Kznza
Xay/On/Nz7Z/Xdj/dz7mlvpx0rTctNOfcE/xjm65AwAAAAAAAADrEuqmH6kd2BylFq6SKWdZGS2y
Zqm0TzZUxnsXWFudhbOSDSVvUsxVjFo5Y0uTvO2O6tAfpd500isHUZO1JLw1nKMTHEqWpTBsrHMu
SZkNS2HLrGaDnfReVLGiiqwKQXOiRVkdcqons8ZBFLJ0NtGsMNhkSBgX5+1KRB1ToKhNYJuc5cTK
KK48EZNSIQXvOVB3078MAAAAAAAAsFlPRUNjLo6LSG1xWDzyw7SeNLMsiudNvw0AAAAAAAAA1uMb
TkMm1wAwAAA=
"""

class TestFarReader(unittest.TestCase):
  def test_example_package(self):
    far_binary_data = gzip.GzipFile(fileobj = io.BytesIO(base64.b64decode(CAT_FAR_GZ))).read()
    far_data = far_read(far_binary_data)
    self.assertTrue("meta/contents" in far_data)
    self.assertTrue("meta/package" in far_data)
    self.assertTrue("bin/cat" in far_data["meta/contents"].decode())
    self.assertTrue("lib/ld.so.1" in far_data["meta/contents"].decode())
    self.assertTrue("lib/libfdio.so" in far_data["meta/contents"].decode())

  def test_invalid_package(self):
    with self.assertRaises(FarFormatError) as exception_context:
      far_data = far_read(INVALID_FILE)
    self.assertTrue("Expected magic number does not match." in str(exception_context.exception))

  def test_invalid_index_len(self):
    with self.assertRaises(FarFormatError) as exception_context:
      far_data = far_read(EOF_INDEX_LEN)
    self.assertEqual("Unexpected EOF parsing far index bytes.", str(exception_context.exception))

  def test_empty(self):
    with self.assertRaises(FarFormatError) as exception_context:
      far_data = far_read(EMPTY_ARCHIVE)
    self.assertEqual("Empty archive.", str(exception_context.exception))
