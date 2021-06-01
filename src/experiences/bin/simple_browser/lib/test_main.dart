// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/services.dart';
import 'package:flutter_driver/driver_extension.dart';

import 'main.dart' as entrypoint;

Future<void> main() async {
  final handler = OptionalMethodChannel('flutter_driver/handler');
  enableFlutterDriverExtension(
      enableTextEntryEmulation: false,
      handler: (String? data) {
        return handler.invokeMethod(data!) as Future<String>;
      });
  entrypoint.main();
}
