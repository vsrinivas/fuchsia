// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// See README.md in this directory for the details about this program.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fidl_examples_echo/fidl_async.dart' as fidl_echo;
import 'package:fuchsia/fuchsia.dart' as fuchsia;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:intl/intl.dart';
import 'package:zircon/zircon.dart';

class _EchoImpl extends fidl_echo.Echo {
  final _binding = fidl_echo.EchoBinding();

  void bind(InterfaceRequest<fidl_echo.Echo> request) {
    log.info('Binding...');
    _binding.bind(this, request);
  }

  @override
  Future<String> echoString(String value) async {
    log.fine('Request: $value');
    final now = DateTime.now();
    final nowLocal = now.toLocal();
    log.info('Test time server thinks that local time is: $nowLocal '
        'and raw time is: $now (tz offset: ${nowLocal.timeZoneOffset}, '
        'seconds since Epoch: ${nowLocal.millisecondsSinceEpoch / 1000.0})');
    // Example: 2020-2-26-14, hour 14 of February 26.
    final dateTime = DateFormat('y-M-d-H').format(nowLocal);
    log.info('Test time server reporting time as: "$dateTime"');
    return dateTime;
  }
}

Future<void> main(List<String> args) async {
  setupLogger(name: 'timestamp_server_dart', globalTags: ['e2e', 'timezone']);
  log.info('Setting up.');

  final context = StartupContext.fromStartupInfo();
  final echo = _EchoImpl();

  var status = context.outgoing
      .addPublicService<fidl_echo.Echo>(echo.bind, fidl_echo.Echo.$serviceName);
  assert(status == ZX.OK);
  log.info('Now serving.');
  // Serve the Echo endpoint for a little while, then exit.
  await Future.delayed(const Duration(minutes: 1));
  log.info('Shutting down.');
  fuchsia.exit(0);
}
