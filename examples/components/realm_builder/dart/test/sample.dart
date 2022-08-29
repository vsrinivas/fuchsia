// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// [START import_statement_dart]
import 'package:fuchsia_component_test/realm_builder.dart';
// [END import_statement_dart]

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fidl_examples_routing_echo/fidl_async.dart' as fecho;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fio;
import 'package:fidl_fuchsia_logger/fidl_async.dart' as flogger;

import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' as services;

import 'package:test/test.dart';

void main() {
  setupLogger(name: 'dart-sample-test');

  // This test demonstrates constructing a realm with two child components
  // and verifying the `fidl.examples.routing.Echo` protocol.
  test('routes_from_echo', () async {
    RealmInstance? realm;
    try {
      // [START init_realm_builder_dart]
      final builder = await RealmBuilder.create();
      // [END init_realm_builder_dart]

      // [START add_component_dart]
      // Add a server component to the realm, which is fetched using an
      // absolute `fuchsia-pkg://` URL.
      final echoServer = await builder.addChild(
        'echo_server',
        'fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_server.cm',
      );

      // Add a child component to the realm using a relative URL. The child is
      // not exposing a service, so the `eager` option ensures the child starts
      // when the realm is built.
      final echoClient = await builder.addChild(
        'echo_client',
        '#meta/echo_client.cm',
        ChildOptions()..eager(),
      );
      // [END add_component_dart]

      // [START route_between_children_dart]
      await builder.addRoute(Route()
        ..capability(ProtocolCapability(fecho.Echo.$serviceName))
        ..from(Ref.child(echoServer))
        ..to(Ref.child(echoClient)));
      // [END route_between_children_dart]

      // [START route_to_test_dart]
      await builder.addRoute(Route()
        ..capability(ProtocolCapability(fecho.Echo.$serviceName))
        ..from(Ref.child(echoServer))
        ..to(Ref.parent()));
      // [END route_to_test_dart]

      // [START route_from_test_dart]
      await builder.addRoute(Route()
        ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
        ..from(Ref.parent())
        ..to(Ref.child(echoServer))
        ..to(Ref.child(echoClient)));
      // [END route_from_test_dart]

      // [START build_realm_dart]
      realm = await builder.build();
      // [END build_realm_dart]

      // [START get_child_name_dart]
      print('Child Name: ${realm.root.childName}');
      // [END get_child_name_dart]

      // [START call_echo_dart]
      final echo = realm.root.connectToProtocolAtExposedDir(fecho.EchoProxy());
      expect(await echo.echoString('hello'), 'hello');
      // [END call_echo_dart]

      // [START finally_close_realm]
    } finally {
      if (realm != null) {
        realm.root.close();
      }
    }
    // [END finally_close_realm]
  });

  // This test demonstrates constructing a realm with a mocked LocalComponent
  // implementation of the `fidl.examples.routing.Echo` protocol.
  test('routes_from_mock_echo', () async {
    RealmInstance? realm;
    try {
      final builder = await RealmBuilder.create();

      // [START add_mock_component_dart]
      final echoServer = await builder.addLocalChild(
        'echo_server',
        onRun: (handles, onStop) async {
          EchoServerMock(handles);

          // Keep the component alive until the test is complete
          await onStop.future;
        },
      );
      // [END add_mock_component_dart]

      await builder.addRoute(Route()
        ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
        ..from(Ref.parent())
        ..to(Ref.child(echoServer)));

      await builder.addRoute(Route()
        ..capability(ProtocolCapability(fecho.Echo.$serviceName))
        ..from(Ref.child(echoServer))
        ..to(Ref.parent()));

      realm = await builder.build();

      final echo = realm.root.connectToProtocolAtExposedDir(fecho.EchoProxy());
      expect(await echo.echoString('hello'), 'hello');
    } finally {
      if (realm != null) {
        realm.root.close();
      }
    }
  });
}

// [START mock_component_impl_dart]
class EchoServerMock extends fecho.Echo {
  final LocalComponentHandles handles;

  final echoBinding = fecho.EchoBinding();

  EchoServerMock(this.handles) {
    // Serve the provided outgoing directory for the mock
    services.Outgoing()
      ..serve(
          fidl.InterfaceRequest<fio.Node>(handles.outgoingDir.passChannel()!))

      // Expose the Echo protocol as a public service
      ..addPublicService(
        (fidl.InterfaceRequest<fecho.Echo> connector) {
          echoBinding.bind(this, connector);
        },
        fecho.Echo.$serviceName,
      );
  }

  @override
  Future<String?> echoString(String? str) async {
    return str;
  }
}
// [END mock_component_impl_dart]
