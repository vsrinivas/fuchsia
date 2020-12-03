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
  final FlutterDriverConnector connector;
  FlutterDriver driver;

  /// Constructor.
  ErmineDriver(this.sl4f) : connector = FlutterDriverConnector(sl4f);

  /// Set up the test environment for Ermine.
  ///
  /// This restarts the workstation session and connects to the running instance
  /// of Ermine using FlutterDriver.
  Future<void> setUp() async {
    // Restart the workstation session.
    final result = await sl4f.ssh.run('session_control restart');
    if (result.exitCode != 0) {
      fail('failed to restart workstation session.');
    }

    // Initialize flutter driver connector.
    await connector.initialize();

    // Now connect to ermine.
    driver = await connector.driverForIsolate('ermine');
    if (driver == null) {
      fail('unable to connect to ermine.');
    }
  }

  /// Closes [FlutterDriverConnector] and performs cleanup.
  Future<void> tearDown() async {
    await driver?.close();
    await connector.tearDown();
  }
}
