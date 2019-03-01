// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show base64Decode;
import 'dart:typed_data' show Uint8List;

import 'package:image/image.dart';

import 'sl4f_client.dart';

class Scenic {
  final Sl4f _sl4f;

  Scenic([Sl4f sl4f]) : _sl4f = sl4f ?? Sl4f.fromEnvironment();

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client is closed instead.
  void close() {
    _sl4f.close();
  }

  Future<Image> takeScreenshot() async {
    final Map<String, dynamic> response =
        await _sl4f.request('scenic_facade.TakeScreenshot');
    final Map<String, dynamic> info = response['info'];

    assert(info['pixel_format'], 'Bgra8');

    final Uint8List bytes = base64Decode(response['data']);

    // Image only takes RGB/RGBA so we need to reverse from BGRA
    for (int quad = 0; quad < bytes.length; quad += 4) {
      final blue = bytes[quad];
      bytes[quad] = bytes[quad + 2];
      bytes[quad + 2] = blue;
    }

    return Image.fromBytes(info['width'], info['height'], bytes);
  }
}
