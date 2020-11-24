// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOTE: The comments that have [START/END ...] in them are used to identify
// code snippets that appear in the documentation. Please be aware that
// changes in these blocks will affect the documentation on fuchsia.dev.

// [START imports]
// The server uses async code to be able to listen for incoming Echo requests and connections
// asynchronously.
import 'dart:async';

// The fidl package contains general utility code for using FIDL in Dart.
import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_examples/fidl_async.dart' as fidl_echo;
// The fuchsia_services package interfaces with the Fuchsia system. In particular, it is used
// to expose a service to other components
import 'package:fuchsia_services/services.dart' as sys;
import 'package:fuchsia_logger/logger.dart';
// [END imports]

// [START impl]
// Create an implementation for the Echo protocol by overriding the
// fidl_echo.Echo class from the bindings
class _EchoImpl extends fidl_echo.Echo {
  // The stream controller for the stream of OnString events
  final _onStringStreamController = StreamController<String>();

  // Implementation of EchoString that just echoes the request value back
  @override
  Future<String> echoString(String value) async {
    log.info('Received EchoString request: $value');
    return value;
  }

  // Implementing of SendString that sends an OnString event back with the
  // request value
  @override
  Future<void> sendString(String value) async {
    log.info('Received SendString request: $value');
    _onStringStreamController.add(value);
  }

  // Returns the stream of OnString events. _binding will listen to this stream
  // and encode and send events to the client.
  @override
  Stream<String> get onString => _onStringStreamController.stream;
}
// [END impl]

// [START main]
void main(List<String> args) {
  setupLogger(name: 'echo-server');

  // Each FIDL protocol class has an accompanying Binding class, which takes
  // an implementation of the protocol and a channel, and dispatches incoming
  // requests on the channel to the protocol implementation.
  final binding = fidl_echo.EchoBinding();

  log.info('Running Echo server');
  // Serves the implementation by passing it a handler for incoming requests,
  // and the name of the protocol it is providing.
  final context = sys.StartupContext.fromStartupInfo();
  final echo = _EchoImpl();
  context.outgoing.addPublicService<fidl_echo.Echo>(
      (fidl.InterfaceRequest<fidl_echo.Echo> serverEnd) =>
          binding.bind(echo, serverEnd),
      fidl_echo.Echo.$serviceName);
}
// [END main]
