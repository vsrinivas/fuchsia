// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/driver_extension.dart';

import 'main.dart' as entrypoint;

Future<void> main() async {
  // Instrument flutter driver handler to invoke shortcut actions.
  enableFlutterDriverExtension(enableTextEntryEmulation: false);

  return entrypoint.main();
}
