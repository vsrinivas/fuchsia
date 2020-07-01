// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const String _dockyardHostPath = 'runtime_deps/dockyard_host';

void main() {
  sl4f.Sl4f sl4fDriver;
  String hostIp;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    // Get our IP address from the device's point of view.
    hostIp = (await sl4fDriver.hostIpAddress()).split('%').first;
    expect(hostIp, isNotEmpty);
    if (hostIp.contains(':')) {
      // It's an ipv6 addr (otherwise it's assumed to be ipv4).
      hostIp = '[$hostIp]';
    }
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('tests that harvester and dockyard can talk', () async {
    // The dockyard_host was copied to a 'generated code' path. Get the full
    // path to that file.
    final fullPath = Platform.script.resolve(_dockyardHostPath).toFilePath();
    expect(fullPath, isNotNull);
    expect(fullPath, isNotEmpty);
    final dockyardHost = await Process.start(fullPath, []);
    try {
      // Detect successful sample upload.
      final receiveDeviceTotalBytes = Completer();
      // Detect when the server is ready to collect data.
      final collecting = Completer();
      dockyardHost.stdout
          .transform(utf8.decoder)
          .transform(LineSplitter())
          .listen((String string) {
        if (string.contains('Starting collecting from')) {
          collecting.complete(true);
        }
        if (string.contains('memory:device_total_bytes')) {
          receiveDeviceTotalBytes.complete(true);
        }
      });
      await collecting.future;
      await sl4f.Component(sl4fDriver)
          .launch('system_monitor_harvester', ['--once', '$hostIp:50051']);
      expect(await receiveDeviceTotalBytes.future, isTrue);
    } finally {
      dockyardHost.kill();
      // Since the dockyard_host is being 'killed', we don't expect a 0 exitCode.
      expect(await dockyardHost.exitCode, -15);
    }
  }, timeout: Timeout(Duration(seconds: 30)));
}
