// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: implementation_imports
import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl_modular;
import 'package:fidl_fuchsia_modular_session/fidl_async.dart';
// TODO(fxb/70818): Add these dependencies back after fuchsia_modular_testing is migrated to fuchisa.git.
// import 'package:fuchsia_modular_testing/test.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart' as fuchsia_sys;
import 'package:fuchsia_modular/src/service_connection/agent_service_connection.dart';
import 'package:test/test.dart';
// TODO(fxb/70818): Add these dependencies back after fuchsia_modular_testing is migrated to fuchisa.git.
// import 'package:fidl_test_modular_dart/fidl_async.dart';

void main() {
  group('connectToAgentService:=', () {
    test('verify should call custom componentContext.connectToAgent', () {
      final mockComponentContext = FakeComponentContextProxy();
      deprecatedConnectToAgentService('agentUrl',
          FakeAsyncProxy('fuchsia.modular.FakeService', r'FakeService'),
          componentContextProxy: mockComponentContext);
      expect(mockComponentContext.calls, 1);
    });

    test('will connect to an agent when no url is provided', () async {
      final agentUrl = generateComponentUrl();
      final sessionmgrConfig = SessionmgrConfig(
        agentServiceIndex: [
          AgentServiceIndexEntry(
              agentUrl: agentUrl, serviceName: Server.$serviceName),
        ],
      );

      final spec = (TestHarnessSpecBuilder()
            ..addComponentToIntercept(agentUrl)
            ..setSessionmgrConfig(sessionmgrConfig))
          .build();

      final harness = await launchTestHarness();

      final server = _ServerImpl();
      final interceptor = AgentInterceptor(harness.onNewComponent)
        ..mockAgent(agentUrl, (agent) {
          agent.exposeService(server);
        });

      await harness.run(spec);

      final componentContext = await getComponentContext(harness);
      final proxy = ServerProxy();
      deprecatedConnectToAgentService(null, proxy,
          componentContextProxy: componentContext);

      final result = await proxy.echo('hello');
      componentContext.ctrl.close();
      proxy.ctrl.close();
      harness.ctrl.close();

      expect(result, 'hello');
      interceptor.dispose();
    });
  });
}

class FakeAsyncProxy<T> extends AsyncProxy<T> {
  String serviceName;
  String interfaceName;
  FakeAsyncProxy(this.serviceName, this.interfaceName)
      : super(AsyncProxyController(
          $serviceName: serviceName,
          $interfaceName: interfaceName,
        ));
}

class FakeComponentContextProxy extends fidl_modular.ComponentContextProxy {
  int calls = 0;

  @override
  Future<void> deprecatedConnectToAgent(
      String url,
      InterfaceRequest<fuchsia_sys.ServiceProvider> incomingServices,
      InterfaceRequest<fidl_modular.AgentController> controller) async {
    calls += 1;
    return;
  }

  @override
  Future<void> deprecatedConnectToAgentService(
      fidl_modular.AgentServiceRequest request) async {
    calls += 1;
  }
}

class _ServerImpl extends Server {
  @override
  Future<String> echo(String value) async {
    return value;
  }
}
