// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/services/shortcuts_service.dart';
import 'package:ermine/src/services/startup_service.dart';
import 'package:flutter_driver/driver_extension.dart';

import 'main.dart' as entrypoint;

Future<void> main() async {
  // Disable screen saver functionality during testing.
  StartupService.allowScreensaver = false;

  // Instrument flutter driver handler to invoke shortcut actions.
  enableFlutterDriverExtension(
      enableTextEntryEmulation: false,
      handler: (String? data) async {
        if (data != null && ShortcutsService.flutterDriverHandler != null) {
          return ShortcutsService.flutterDriverHandler!.call(data);
        }
        return '';
      });
  return entrypoint.main();
}
