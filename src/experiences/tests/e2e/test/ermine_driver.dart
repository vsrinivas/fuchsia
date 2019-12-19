// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Defines a test utility class to drive Ermine during integration test using
/// Flutter Driver. This utility will grow with more convenience methods in the
/// future useful for testing.
class ErmineDriver {
  final Sl4f sl4f;
  FlutterDriverConnector connector;

  ErmineDriver(this.sl4f);

  /// Connect to the isolate for Ermine and returns the [FlutterDriver]
  /// instance. The instance should be closed when done.
  Future<FlutterDriver> connect() async {
    connector = FlutterDriverConnector(sl4f);
    await connector.initialize();

    // Check if ermine is running.
    final isolate = await connector.isolate('ermine');
    if (isolate == null) {
      // Use `sessionctl` to login as guest and start ermine.
      await sl4f.ssh.run('sessionctl restart_session');
      final result = await sl4f.ssh.run('sessionctl login_guest');
      if (result.exitCode != 0) {
        fail('unable to login guest - check user already logged in?');
      }
    }

    // Now connect to ermine.
    final driver = await connector.driverForIsolate('ermine');
    if (driver == null) {
      fail('unable to connect to ermine.');
    }

    return driver;
  }

  /// Closes [FlutterDriverConnector] and performs cleanup.
  Future<void> close() async {
    await connector.tearDown();
  }
}
