// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_test_fuchsia_flutter/fidl_async.dart' as fidl_ping;
import 'package:fidl/fidl.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_logger/logger.dart';

void main() {
  setupLogger(name: 'pingable-flutter-component');
  log.info('Starting pingable-flutter-component');

  // Launch a simple ping server so that we can connect to it in our tests
  final pinger = _PingerImpl();

  StartupContext.fromStartupInfo()
      .outgoing
      .addPublicService(pinger.bind, fidl_ping.Pinger.$serviceName);

  runApp(
    MaterialApp(
      home: Scaffold(
        body: Container(
          child: Center(
            child: Text('I am a pingable Flutter component!'),
          ),
        ),
      ),
    ),
  );
}

class _PingerImpl extends fidl_ping.Pinger {
  final _binding = fidl_ping.PingerBinding();

  /// Bind the request to our binding
  void bind(InterfaceRequest<fidl_ping.Pinger> request) {
    log.info('Calling bind on the ping server');
    _binding.bind(this, request);
  }

  @override
  Future<void> ping() async {
    log.info('got ping request');
  }
}
