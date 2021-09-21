// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// [START imports]
import 'dart:async';

import 'package:fidl_fuchsia_examples/fidl_async.dart' as fidl_echo;
import 'package:fuchsia/fuchsia.dart' show exit;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
// [END imports]

// [START main]
Future<void> main(List<String> args) async {
  // Create our component context and serve the outgoing directory.
  final context = ComponentContext.createAndServe();
  setupLogger(name: 'echo-client');

  // Bind. We bind EchoProxy, a generated proxy class, to the remote Echo
  // service.
  final client = fidl_echo.EchoProxy();
  context.svc.connectToService(client);

  // Invoke echoString with a value and print its response.
  final response = await client.echoString('hello');
  log.info('Got response: $response');

  // Invoke sendString, which does not have a response
  await client.sendString('hi');
  // Wait for one OnString event and print its value.
  final event = await client.onString.first;
  log.info('Got event: $event');

  // Allow log messages to get piped through to the syslogger before exiting
  // and killing this process
  await Future(() => exit(0));
}
// [END main]
