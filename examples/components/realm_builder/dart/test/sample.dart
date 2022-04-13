// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fuchsia_component_test/realm_builder.dart';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fidl_examples_routing_echo/fidl_async.dart' as fecho;
import 'package:fidl_fuchsia_component/fidl_async.dart' as fcomponent;
import 'package:fidl_fuchsia_component_test/fidl_async.dart' as fctest;
import 'package:fidl_fuchsia_logger/fidl_async.dart' as flogger;
import 'package:fidl_fuchsia_sys2/fidl_async.dart' as fsys2;

import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' as services;

import 'package:test/test.dart';

const String v2EchoClientUrl = '#meta/echo_client.cm';
const String v2EchoServerUrl = '#meta/echo_server.cm';

void checkCommonExceptions(Exception err, StackTrace stacktrace) {
  if (err is fidl.MethodException<fcomponent.Error>) {
    late final String errorName;
    for (final name in fcomponent.Error.$valuesMap.keys) {
      if (err.value == fcomponent.Error.$valuesMap[name]) {
        errorName = name;
        break;
      }
    }
    log.warning('fidl.$err: fuchsia.component.Error.$errorName');
  } else if (err is fidl.MethodException<fctest.RealmBuilderError2>) {
    late final String errorName;
    for (final name in fctest.RealmBuilderError2.$valuesMap.keys) {
      if (err.value == fctest.RealmBuilderError2.$valuesMap[name]) {
        errorName = name;
        break;
      }
    }
    log.warning('fidl.$err: fuchsia.component.test.Error.$errorName');
  } else if (err is fidl.MethodException) {
    log.warning('fidl.MethodException<${err.value.runtimeType}>($err)');
  } else if (err is fidl.FidlError) {
    log.warning('fidl.${err.runtimeType}($err), FidlErrorCode: ${err.code}');
  } else {
    log.warning('caught exception: ${err.runtimeType}($err)');
  }
  log.warning('stacktrace (if available)...\n${stacktrace.toString()}');
}

void main() {
  setupLogger(name: 'sample');

  test('route echo between two v2 components', () async {
    final eventStreamBinding = fsys2.EventStreamBinding();
    RealmInstance? realmInstance;
    try {
      final builder = await RealmBuilder.create();

      const echoServerName = 'v2EchoServer';
      const echoClientName = 'v2EchoClient';

      final v2EchoServer = await builder.addChild(
        echoServerName,
        v2EchoServerUrl,
      );
      final v2EchoClient = await builder.addChild(
        echoClientName,
        v2EchoClientUrl,
        ChildOptions()..eager(),
      );

      // Route logging to children
      await builder.addRoute(Route()
        ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
        ..from(Ref.parent())
        ..to(Ref.child(v2EchoServer))
        ..to(Ref.child(v2EchoClient)));

      // Route the echo service from server to client
      await builder.addRoute(Route()
        ..capability(ProtocolCapability(fecho.Echo.$serviceName))
        ..from(Ref.child(v2EchoServer))
        ..to(Ref.child(v2EchoClient)));

      // Route the framework's EventSource so the test can await the echo
      // client's termination and verify a successful exit status.
      await builder.addRoute(Route()
        ..capability(ProtocolCapability(fsys2.EventSource.$serviceName))
        ..from(Ref.framework())
        ..to(Ref.parent()));

      // Connect to the framework's EventSource.
      final eventSource = fsys2.EventSourceProxy();
      await (services.Incoming.fromSvcPath()..connectToService(eventSource))
          .close();

      // Register a callback for stopped events, and complete a Future when
      // the event client stops.
      final completeWaitForStop = Completer<int>();
      final eventStreamClientEnd = eventStreamBinding.wrap(
        OnEvent(stopped: (String moniker, int status) {
          // Since EchoClient is [eager()], it may start and stop before the
          // async [builder.build()] completes. [realmInstance.root.childName]
          // would not be known before this stopped event is received, so
          // [endsWith()] is the best solution here.
          if (moniker.endsWith('/$echoClientName')) {
            completeWaitForStop.complete(status);
          }
        }),
      );

      // Subscribe for "stopped" events.
      //
      // NOTE: This requires the test CML include a `use` for the subscribed
      // event type(s), for example:
      //
      // ```cml
      //   use: [
      //     { protocol: "fuchsia.sys2.EventSource" },
      //     {
      //         event: [
      //             "started",
      //             "stopped",
      //         ],
      //         from: "framework",
      //     },
      //   ],
      // ```
      await eventSource.subscribe(
        [fsys2.EventSubscription(eventName: 'stopped')],
        eventStreamClientEnd,
      );

      // Start the realm instance.
      realmInstance = await builder.build();

      // Wait for the client to stop, and check for a successful exit status.
      final stoppedStatus = await completeWaitForStop.future;
      expect(stoppedStatus, 0);
    } on Exception catch (err, stacktrace) {
      checkCommonExceptions(err, stacktrace);
      rethrow;
    } finally {
      if (realmInstance != null) {
        realmInstance.root.close();
      }
      eventStreamBinding.close();
    }
  });
}
