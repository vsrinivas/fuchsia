// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show utf8;
import 'dart:math' show min;
import 'dart:typed_data';

/// Converts a [String] to byte data containing utf8.
///
/// If [maxBytes] is specified, this function may truncate (malform) the last
/// character if it's multibyte in utf8.
ByteData toByteData(String string, {int maxBytes = -1}) {
  var bytes = utf8.encode(string);
  var length = bytes.length;
  if (maxBytes >= 0) {
    length = min(length, maxBytes);
  }
  var byteData = ByteData(length);
  for (int i = 0; i < length; i++) {
    byteData.setUint8(i, bytes[i]);
  }
  return byteData;
}
