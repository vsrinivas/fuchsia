// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_modular_testing/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart' as fidl_sys;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/agent.dart';
import 'package:fuchsia_modular/src/agent/internal/_agent_impl.dart'; // ignore: implementation_imports
import 'package:fuchsia_modular/src/lifecycle/internal/_lifecycle_impl.dart'; // ignore: implementation_imports

import 'test_harness_fixtures.dart';

/// A function which is called when a new agent that is being launched.
typedef OnNewAgent = void Function(Agent? agent);

/// A helper class for managing the intercepting of agents inside the
/// [TestHarness].
///
/// When agents which are registered to be mocked are launchedm the [OnNewAgent]
/// function will be executed allowing developers to expose services.
///
/// ```
/// AgentInterceptor(testHarness.onNewComponent)
///   .mockAgent(agentUrl, (agent) {
///     agent.exposeService(myService);
///   });
///
/// deprecatedConnectToAgentService(agentUrl, myServiceProxy,
///     componentContextProxy, await getComponentContext(harness));
/// ```
class AgentInterceptor {
  final _registeredAgents = <String, OnNewAgent>{};
  final _mockedAgents = <String, _MockedAgent>{};
  late StreamSubscription<TestHarness$OnNewComponent$Response>
      _streamSubscription;

  /// Creates an instance of this which will listen to the [onNewComponentStream].
  AgentInterceptor(
      Stream<TestHarness$OnNewComponent$Response> onNewComponentStream) {
    _streamSubscription = onNewComponentStream.listen(_handleResponse);
  }

  /// Dispose of the interceptor.
  ///
  /// This method will cancel the onNewComponent stream subscription.
  /// The object is no longer valid after this method is called.
  void dispose() {
    _streamSubscription.cancel();
  }

  /// Register an [agentUrl] to be mocked.
  ///
  /// If a component with the component url which matches [agentUrl] is
  /// registered to be interecepted by the test harness [onNewAgent] will be
  /// called when that component is first launched. The [onNewAgent] method will
  /// be called with an injected [Agent] object. This method can be treated like
  /// a normal main method in a non mocked agent.
  void mockAgent(String? agentUrl, OnNewAgent? onNewAgent) {
    ArgumentError.checkNotNull(agentUrl, 'agentUrl');
    ArgumentError.checkNotNull(onNewAgent, 'onNewAgent');

    if (agentUrl!.isEmpty) {
      throw ArgumentError('agentUrl must not be empty');
    }

    if (_registeredAgents.containsKey(agentUrl)) {
      throw Exception(
          'Attempting to add [$agentUrl] twice. Agent urls must be unique');
    }
    _registeredAgents[agentUrl] = onNewAgent!;
  }

  /// This method is called by the listen method when this object is used as the
  /// handler to the [TestHarnessProxy.onNewComponent] stream.
  void _handleResponse(TestHarness$OnNewComponent$Response response) {
    final startupInfo = response.startupInfo;
    final componentUrl = startupInfo.launchInfo.url;
    if (_registeredAgents.containsKey(componentUrl)) {
      final mockedAgent = _MockedAgent(
        startupInfo: startupInfo,
        interceptedComponentRequest: response.interceptedComponent,
      );
      _mockedAgents[componentUrl] = mockedAgent;
      _registeredAgents[componentUrl]!(mockedAgent.agent);
    } else {
      log.info(
          'Skipping launched component [$componentUrl] because it was not registered');
    }
  }
}

/// A helper class which helps manage the lifecyle of a mocked agent
class _MockedAgent {
  /// The intercepted component. This object can be used to control the
  /// launched component.
  final InterceptedComponentProxy interceptedComponent =
      InterceptedComponentProxy();

  /// The instance of the [Agent] which is running in this environment
  AgentImpl? agent;

  /// The lifecycle service for this environment
  LifecycleImpl? lifecycle;

  _MockedAgent({
    required fidl_sys.StartupInfo startupInfo,
    required InterfaceHandle<InterceptedComponent> interceptedComponentRequest,
  }) {
    final outgoing = createComponentContext(startupInfo).outgoing;
    agent = AgentImpl()..serve(outgoing);

    // Note: we want to have a exitHandler which does not call exit here
    // because this mocked agent is running inside the test process and
    // calling fuchsia.exit will cause the test process to close.
    lifecycle = LifecycleImpl(outgoing: outgoing, exitHandler: (_) {})
      ..addTerminateListener(() async {
        interceptedComponent.ctrl.close();
      });

    interceptedComponent.ctrl.bind(interceptedComponentRequest);
  }
}
