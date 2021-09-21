// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert' show base64Decode;

import 'package:image/image.dart';
import 'package:logging/logging.dart';
import 'package:pedantic/pedantic.dart';

import 'device_log.dart';
import 'dump.dart';
import 'sl4f_client.dart';

final _log = Logger('Scenic');

/// Interact with Scenic on the device under test.
class Scenic {
  final Sl4f _sl4f;
  final Dump _dump;
  final DeviceLog _deviceLog;

  /// Constructs a [Scenic] object.
  Scenic(this._sl4f, [Dump dump])
      : _dump = dump ?? Dump(),
        _deviceLog = DeviceLog(_sl4f);

  /// Captures the screen of the device.
  ///
  /// Returns the screenshot as an [Image]. If a [dumpName] is provided, the
  /// PNG is also dumped with that name as prefix.
  Future<Image> takeScreenshot({String dumpName}) async {
    final Map<String, dynamic> response =
        await _sl4f.request('scenic_facade.TakeScreenshot');
    final Map<String, dynamic> info = response['info'];

    final image = Image.fromBytes(
        info['width'], info['height'], base64Decode(response['data']),
        format: Format.bgra);

    if (dumpName != null) {
      await _deviceLog.info('writing screenshot file to $dumpName');
      _log.info('writing screenshot file to $dumpName');
      unawaited(_dump.writeAsBytes(dumpName, 'png', encodePng(image)));
    }

    return image;
  }
}
