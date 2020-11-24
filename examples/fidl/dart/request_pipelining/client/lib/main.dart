// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:pedantic/pedantic.dart' show unawaited;
import 'package:fidl_fuchsia_examples/fidl_async.dart' as fidl_echo;
import 'package:fuchsia_services/services.dart' as sys;
import 'package:fuchsia/fuchsia.dart' show exit;
import 'package:fuchsia_logger/logger.dart';

// [START main]
Future<void> main(List<String> args) async {
  setupLogger(name: 'echo-launcher-client');
  final svc = sys.StartupContext.fromStartupInfo().incoming;

  // Connect to the EchoLauncher service
  final echoLauncher = fidl_echo.EchoLauncherProxy();
  svc.connectToService(echoLauncher);

  // Non pipelined case: wait for EchoLauncher to respond with a client end, then bind it to the
  // proxy and make an EchoString request
  final nonPipelinedFut =
      echoLauncher.getEcho('not pipelined: ').then((clientEnd) async {
    final nonPipelinedEcho = fidl_echo.EchoProxy()..ctrl.bind(clientEnd);
    final response = await nonPipelinedEcho.echoString('hello');
    log.info('Got echo response $response');
  });

  final pipelinedEcho = fidl_echo.EchoProxy();
  // Pipelined case: make a request with the server end of the proxy
  unawaited(echoLauncher.getEchoPipelined(
      'pipelined: ', pipelinedEcho.ctrl.request()));
  // Then, make an EchoString request with the proxy without needing to wait for
  // a response.
  final pipelinedFut = pipelinedEcho
      .echoString('hello')
      .then((response) => log.info('Got echo response $response'));

  // Run the two futures concurrently.
  await Future.wait([nonPipelinedFut, pipelinedFut]);
  await Future(() => exit(0));
}
// [END main]
