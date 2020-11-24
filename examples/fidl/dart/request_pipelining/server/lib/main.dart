// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_examples/fidl_async.dart' as fidl_echo;
import 'package:fuchsia_services/services.dart';
import 'package:meta/meta.dart';
import 'package:fuchsia_logger/logger.dart';

// [START echo-impl]
// Implementation of Echo that responds with a prefix prepended to each response
class _EchoImpl extends fidl_echo.Echo {
  // The EchoBinding is added as a member to make serving the protocol easier.
  final _binding = fidl_echo.EchoBinding();
  final String prefix;

  _EchoImpl({@required this.prefix}) : assert(prefix != null);

  void bind(fidl.InterfaceRequest<fidl_echo.Echo> request) {
    _binding.bind(this, request);
  }

  // Reply to EchoString with a possibly reversed string
  @override
  Future<String> echoString(String value) async {
    return prefix + value;
  }

  // SendString isn't used for the purposes of this example
  @override
  Future<void> sendString(String value) async {}

  // OnString isn't used for the purposes of this example, so just return an empty stream
  @override
  Stream<String> get onString => Stream.empty();
}
// [END echo-impl]

// [START launcher-impl]
// Implementation of EchoLauncher that will launch an Echo instance that
// responds with the specified prefix.
class _EchoLauncherImpl extends fidl_echo.EchoLauncher {
  final List<_EchoImpl> servers = [];

  // For the non pipelined method, the server needs to create a channel pair,
  // bind an Echo server to the server end, then send the client end back to the
  // client
  @override
  Future<fidl.InterfaceHandle<fidl_echo.Echo>> getEcho(String prefix) async {
    final echoPair = fidl.InterfacePair<fidl_echo.Echo>();
    final serverEnd = echoPair.passRequest();
    final clientEnd = echoPair.passHandle();

    launchEchoServer(prefix, serverEnd);
    return clientEnd;
  }

  // For the pipelined method, the client provides the server end of the channel
  // so we can simply call launchEchoServer
  @override
  Future<void> getEchoPipelined(
      String prefix, fidl.InterfaceRequest<fidl_echo.Echo> serverEnd) async {
    launchEchoServer(prefix, serverEnd);
  }

  // Launches a new echo server that uses the specified prefix, and binds it to
  // the provided InterfaceRequest. Each launched server is stored in the
  // servers member so that it doesn't get garbage collected.
  void launchEchoServer(
      String prefix, fidl.InterfaceRequest<fidl_echo.Echo> serverEnd) {
    servers.add(_EchoImpl(prefix: prefix)..bind(serverEnd));
  }
}
// [END launcher-impl]

// [START main]
void main(List<String> args) {
  setupLogger(name: 'echo-launcher-server');
  final context = StartupContext.fromStartupInfo();
  final echoLauncher = _EchoLauncherImpl();
  final binding = fidl_echo.EchoLauncherBinding();

  log.info('Running EchoLauncher server');
  context.outgoing.addPublicService<fidl_echo.EchoLauncher>(
      (fidl.InterfaceRequest<fidl_echo.EchoLauncher> serverEnd) =>
          binding.bind(echoLauncher, serverEnd),
      fidl_echo.EchoLauncher.$serviceName);
}
// [END main]
