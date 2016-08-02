// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import '../base/process.dart';
import '../constants.dart';
import '../configuration.dart';
import 'modular_command.dart';

class ClearCacheCommand extends ModularCommand {
  final String name = 'clear-cache';
  final String description = 'Clear Modular caches.';

  @override
  Future<int> runInProject() {
    switch (target) {
      case TargetPlatform.linux:
        return process('rm',
            ['-rf', '~/.mojo_url_response_disk_cache', '~/.modular-dev',]);
      case TargetPlatform.android:
        // TODO(alhaad): Do not hard-code the path to android_tools.
        return process('./third_party/android_tools/sdk/platform-tools/adb', [
          'shell',
          'rm',
          '-rf',
          '/data/data/org.chromium.mojo.shell/cache',
          kAndroidHomeDir,
        ]);
      default:
        return new Future.value(1);
    }
  }
}
