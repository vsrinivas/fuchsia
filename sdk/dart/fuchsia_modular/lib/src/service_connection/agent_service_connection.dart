// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: avoid_as

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl_modular;
import 'package:fuchsia_logger/logger.dart';

import '../internal/_component_context.dart';

/// Connect to the service specified by [serviceProxy] and implemented by the
/// agent with [agentUrl]. Optionally, provide a [componentContextProxy] which
/// will be used to connect to the agent. If [agentUrl] is null then the framework
/// will attempt to automatically resolve an appropriate agent for the service.
///
/// The agent will be launched if it's not already running.
void deprecatedConnectToAgentService<T>(
    String? agentUrl, AsyncProxy<T>? serviceProxy,
    {fidl_modular.ComponentContextProxy? componentContextProxy}) {
  if (serviceProxy == null) {
    throw Exception(
        'serviceProxy must not be null in call to connectToAgentService');
  }

  final serviceName = serviceProxy.ctrl.$serviceName;
  if (serviceName == null) {
    throw Exception("${serviceProxy.ctrl.$interfaceName}'s "
        'proxyServiceController.\$serviceName must not be null. Check the FIDL '
        'file for a missing [Discoverable]');
  }

  final agentControllerProxy = fidl_modular.AgentControllerProxy();

  // Creates an interface request and binds one of the channels. Binding this
  // channel prior to connecting to the agent allows the developer to make
  // proxy calls without awaiting for the connection to actually establish.
  final serviceProxyRequest = serviceProxy.ctrl.request();

  final agentServiceRequest = fidl_modular.AgentServiceRequest(
    serviceName: serviceName,
    channel: serviceProxyRequest.passChannel(),
    handler: agentUrl,
    agentController: agentControllerProxy.ctrl.request(),
  );

  serviceProxy.ctrl.whenClosed.then((_) {
    agentControllerProxy.ctrl.close();
  });

  componentContextProxy ??=
      getComponentContext() as fidl_modular.ComponentContextProxy?;

  componentContextProxy!
      .deprecatedConnectToAgentService(agentServiceRequest)
      .catchError((e) {
    log.shout('Failed to connect to agent service [$serviceName]', e);
  });
}

/// TODO(fxbug.dev/49976): Remove this once clients have been migrated
/// to use deprecatedConnectToAgentService().
void connectToAgentService<T>(String agentUrl, AsyncProxy<T> serviceProxy,
    {fidl_modular.ComponentContextProxy? componentContextProxy}) {
  deprecatedConnectToAgentService(agentUrl, serviceProxy,
      componentContextProxy: componentContextProxy);
}
