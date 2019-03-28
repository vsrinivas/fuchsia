// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show base64Decode;
import 'dart:io' show Directory, File, FileSystemException, Platform;
import 'dart:typed_data' show Uint8List;

import 'package:image/image.dart' show encodePng, Image;

import 'sl4f_client.dart';

class Scenic {
  /// Environment variable for the directory to dump images in. If this var is
  /// present, screenshots can optionally be dumped in there.
  static const _dumpDirectoryEnvVar = 'FUCHSIA_TEST_OUTDIR';

  final Sl4f _sl4f;

  /// Directory to dump screenshots taken.
  final String _dumpDirectory;

  /// Constructs a [Scenic] object.
  ///
  /// It can optionally take an [Sl4f] object, if not passed, one will be
  /// created using the environment variables to connect to the device. If a
  /// [dumpDirectory] is specified, or if the FUCHSIA_TEST_OUTDIR env variable
  /// is defined, screenshots can be dumped there using [dumpImage].
  Scenic([Sl4f sl4f, String dumpDirectory])
      : _sl4f = sl4f ?? Sl4f.fromEnvironment(),
        _dumpDirectory =
            dumpDirectory ?? Platform.environment[_dumpDirectoryEnvVar] {
    // Has to be sync because this is a constructor.
    if (_dumpDirectory != null && !Directory(_dumpDirectory).existsSync()) {
      throw FileSystemException('Not found or not a directory', dumpDirectory);
    }
  }

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client is closed instead.
  void close() {
    _sl4f.close();
  }

  /// Captures the screen of the device.
  ///
  /// Returns an Image in RGBA format. If a [dumpName] is provided, the picture
  /// is also dumped with that name as prefix.
  Future<Image> takeScreenshot({String dumpName}) async {
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

    final image = Image.fromBytes(info['width'], info['height'], bytes);
    if (dumpName != null) {
      // Write to the file asynchronously so the test can continue.
      // ignore: unawaited_futures
      dumpImage(image, dumpName);
    }
    return image;
  }

  /// Dumps the image to the dump directory if specified.
  Future<void> dumpImage(Image image, [String name]) async {
    if (_dumpDirectory == null) {
      return;
    }

    final filename =
        '${DateTime.now().toIso8601String()}-${name ?? 'screenshot'}.png';

    await File([_dumpDirectory, filename].join('/'))
        .writeAsBytes(encodePng(image));
  }
}
