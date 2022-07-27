// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart' as dbtest;
import 'package:fidl_fuchsia_logger/fidl_async.dart' as flogger;
import 'package:fuchsia_component_test/realm_builder.dart';

// Note, Dart components derive a default binary name from the component name
// ("server") at build time, and the runner derives the component name from its
// moniker (passed to `addChild()`), so they must match.
const _kComponentName = 'server';
const _kServerUrl = '#meta/${_kComponentName}.cm';

class TestServerInstance {
  final dbtest.TestServerProxy proxy = dbtest.TestServerProxy();
  RealmInstance realm;

  Future<void> start() async {
    final builder = await RealmBuilder.create();

    final serverRef = await builder.addChild(_kComponentName, _kServerUrl);

    await builder.addRoute(Route()
      ..capability(ProtocolCapability(flogger.LogSink.$serviceName))
      ..from(Ref.parent())
      ..to(Ref.child(serverRef)));

    await builder.addRoute(Route()
      ..capability(ProtocolCapability(dbtest.TestServer.$serviceName))
      ..from(Ref.child(serverRef))
      ..to(Ref.parent()));

    realm = await builder.build();

    realm.root.connectToProtocolAtExposedDir(proxy);
  }

  Future<void> stop() async {
    if (realm != null) {
      realm.root.close();
      realm = null;
    }
  }
}
