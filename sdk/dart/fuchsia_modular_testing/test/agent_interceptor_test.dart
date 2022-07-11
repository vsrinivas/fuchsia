// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_modular_session/fidl_async.dart';
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart';
import 'package:fidl_test_modular_dart/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/service_connection.dart';
// ignore_for_file: implementation_imports
import 'package:fuchsia_modular_testing/test.dart';
import 'package:test/test.dart';

void main() {
  setupLogger();

  group('mock registration', () {
    late AgentInterceptor agentInterceptor;

    setUp(() {
      agentInterceptor =
          AgentInterceptor(Stream<TestHarness$OnNewComponent$Response>.empty());
    });

    test('mockAgent throws for null agentUrl', () {
      expect(
          () => agentInterceptor.mockAgent(null, (_) {}), throwsArgumentError);
    });

    test('mockAgent throws for empty agentUrl', () {
      expect(() => agentInterceptor.mockAgent('', (_) {}), throwsArgumentError);
    });

    test('mockAgent throws for missing callback', () {
      expect(() => agentInterceptor.mockAgent(generateComponentUrl(), null),
          throwsArgumentError);
    });

    test('mockAgent throws for registering agent twice', () {
      final agentUrl = generateComponentUrl();
      void callback(_) {}

      agentInterceptor.mockAgent(agentUrl, callback);

      expect(() => agentInterceptor.mockAgent(agentUrl, callback),
          throwsException);
    });
  });

  group('agent intercepting', () {
    late TestHarnessProxy harness;
    late String agentUrl;
    late AgentInterceptor interceptor;

    setUp(() async {
      agentUrl = generateComponentUrl();
      harness = await launchTestHarness();
      interceptor = AgentInterceptor(harness.onNewComponent);
    });

    tearDown(() {
      harness.ctrl.close();
      interceptor.dispose();
    });

    test('onNewAgent called for mocked agent', () async {
      final sessionmgrConfig = SessionmgrConfig(sessionAgents: [agentUrl]);

      final spec = (TestHarnessSpecBuilder()
            ..addComponentToIntercept(agentUrl)
            ..setSessionmgrConfig(sessionmgrConfig))
          .build();

      final didCallCompleter = Completer<bool>();
      interceptor.mockAgent(agentUrl, (agent) {
        expect(agent, isNotNull);
        didCallCompleter.complete(true);
      });

      await harness.run(spec);

      final componentContext = await getComponentContext(harness);
      final proxy = ServerProxy();
      deprecatedConnectToAgentService(agentUrl, proxy,
          componentContextProxy: componentContext);
      componentContext.ctrl.close();
      proxy.ctrl.close();

      expect(await didCallCompleter.future, isTrue);
    });

    test('onNewAgent can expose a service', () async {
      final sessionmgrConfig = SessionmgrConfig(sessionAgents: [agentUrl]);
      final spec = (TestHarnessSpecBuilder()
            ..addComponentToIntercept(agentUrl)
            ..setSessionmgrConfig(sessionmgrConfig))
          .build();

      final server = _ServerImpl();
      interceptor.mockAgent(agentUrl, (agent) {
        agent!.exposeService(server);
      });

      await harness.run(spec);

      final fooProxy = ServerProxy();
      final componentContext = await getComponentContext(harness);
      deprecatedConnectToAgentService(agentUrl, fooProxy,
          componentContextProxy: componentContext);

      expect(await fooProxy.echo('some value'), 'some value');

      fooProxy.ctrl.close();
      componentContext.ctrl.close();
    });

    test('onNewAgent can expose a service generically', () async {
      final sessionmgrConfig = SessionmgrConfig(sessionAgents: [agentUrl]);
      final spec = (TestHarnessSpecBuilder()
            ..addComponentToIntercept(agentUrl)
            ..setSessionmgrConfig(sessionmgrConfig))
          .build();

      final interceptors = <AgentInterceptor>[];
      for (final server in <fidl.Service>[_ServerImpl()]) {
        interceptors.add(AgentInterceptor(harness.onNewComponent)
          ..mockAgent(agentUrl, (agent) {
            agent!.exposeService(server);
          }));
      }

      await harness.run(spec);

      final fooProxy = ServerProxy();
      final componentContext = await getComponentContext(harness);
      deprecatedConnectToAgentService(agentUrl, fooProxy,
          componentContextProxy: componentContext);

      expect(await fooProxy.echo('some value'), 'some value');

      fooProxy.ctrl.close();
      componentContext.ctrl.close();

      for (final agentInterceptor in interceptors) {
        agentInterceptor.dispose();
      }
    });
  }, skip: true);
  // TODO(fxbug.dev/52285): This test times out in CQ
}

class _ServerImpl extends Server {
  @override
  Future<String?> echo(String? value) async {
    ArgumentError.checkNotNull(value, 'value');

    return value;
  }
}
