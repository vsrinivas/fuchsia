// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

/// Provides utility functions to encode to and decode from Base64.
class Base64 {
  /// Returns the url-safe Base64 encoding of the given [text].
  static String encodeString(String text) => encodeList(UTF8.encode(text));

  /// Returns the url-safe Base64 encoding of the given [data].
  static String encodeList(List<int> data) =>
      BASE64.encode(data).replaceAll('+', '-').replaceAll('/', '_');

  /// Decodes the given [base64] and returns its String representation.
  static String decodeToString(String base64) =>
      UTF8.decode(decodeToList(base64));

  /// Decodes the given [base64] and returns its List representation.
  static List<int> decodeToList(String base64) => BASE64.decode(base64);
}
