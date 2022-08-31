// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_test_fuchsia_service_foo/fidl_async.dart';
import 'package:fuchsia_component_test/realm_builder.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:test/test.dart';

const String serverUrl = '#meta/fuchsia-services-foo-test-server.cm';

void main() {
  setupLogger(name: 'fuchsia-services-test');
  test('launching and connecting to the foo service', () async {
    final builder = await RealmBuilder.create();
    final server = await builder.addChild(
      'fuchsia-services-foo-test-server',
      serverUrl,
    );
    await builder.addRoute(Route()
      ..capability(ProtocolCapability(Foo.$serviceName))
      ..from(Ref.child(server))
      ..to(Ref.parent()));
    final instance = await builder.build();

    final incoming = Incoming.withDirectory(instance.root.exposedDir);
    final fooProxy = FooProxy();
    incoming.connectToService(fooProxy);
    final response = await fooProxy.echo('foo');
    expect(response, 'foo');
  });
}
