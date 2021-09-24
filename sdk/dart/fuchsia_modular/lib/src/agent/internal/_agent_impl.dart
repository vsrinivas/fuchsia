// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;
import 'package:fidl_fuchsia_sys/fidl_async.dart' as fidl_sys;
import 'package:fuchsia_modular/lifecycle.dart';
import 'package:fuchsia_services/services.dart';

import '../agent.dart';

/// A concrete implementation of the [Agent] interface.
///
/// This class is not intended to be used directly by authors but instead
/// should be used by the [Agent] factory constructor.
class AgentImpl extends fidl.Agent implements Agent {
  /// Holds the framework binding connection to this agent.
  final fidl.AgentBinding _agentBinding = fidl.AgentBinding();

  /// The service provider which can be used to expose outgoing services
  final ServiceProviderImpl _serviceProvider;

  /// Holds the connection of other components to this agent's service provider
  final List<fidl_sys.ServiceProviderBinding> _serviceProviderBindings =
      <fidl_sys.ServiceProviderBinding>[];

  /// Holds the outgoing connection of other components to the services provided
  /// by this agent.
  final List<AsyncBinding<Object?>> _outgoingServicesBindings =
      <AsyncBinding<Object>>[];

  /// The default constructor for this instance.
  AgentImpl({Lifecycle? lifecycle, ServiceProviderImpl? serviceProviderImpl})
      : _serviceProvider = serviceProviderImpl ?? ServiceProviderImpl() {
    (lifecycle ??= Lifecycle()).addTerminateListener(_terminate);
  }

  @override
  Future<void> connect(
    String requestorUrl,
    InterfaceRequest<fidl_sys.ServiceProvider> services,
  ) async {
    // Bind this agent's serviceProvider to the client request.
    //
    // Note: currently we're ignoring the [requestorUrl] and providing the same
    // set of services to all clients.
    _serviceProviderBindings.add(
        fidl_sys.ServiceProviderBinding()..bind(_serviceProvider, services));
  }

  @override
  void exposeService<T extends Service>(FutureOr<T>? serviceImpl) {
    if (serviceImpl == null) {
      throw ArgumentError.notNull('serviceImpl');
    }

    if (serviceImpl is Future) {
      Future.value(serviceImpl).then((T service) {
        exposeServiceProvider(() => service, service.$serviceData);
      });
    } else {
      exposeServiceProvider(() => serviceImpl, serviceImpl.$serviceData);
    }
  }

  @override
  void exposeServiceProvider<T extends Service>(
      FutureOr<ServiceProvider<T>>? serviceProvider, ServiceData? serviceData) {
    if (serviceProvider == null) {
      throw ArgumentError.notNull('serviceProvider');
    }
    if (serviceData == null) {
      throw ArgumentError.notNull('serviceData');
    }

    // Add this [serviceImpl] to this agent's serviceProvider so that it can
    // be accessed the `connected clients of this agent.
    _serviceProvider.addServiceForName(
      (InterfaceRequest<T> request) {
        Future.value(serviceProvider)
            .then((ServiceProvider<T> providerFunc) => providerFunc()!)
            .then((FutureOr<T>? service) => _outgoingServicesBindings
                .add(serviceData.getBinding()..bind(service, request)));
      },
      serviceData.getName(),
    );
  }

  @override
  void serve(Outgoing outgoing) {
    outgoing.addPublicService(
      (InterfaceRequest<fidl.Agent> request) {
        assert(!_agentBinding.isBound);
        _agentBinding.bind(this, request);
      },
      fidl.Agent.$serviceName,
    );
  }

  // Any necessary cleanup should be done here.
  Future<void> _terminate() async {
    _agentBinding.close();
    for (fidl_sys.ServiceProviderBinding binding in _serviceProviderBindings) {
      binding.close();
    }
    for (AsyncBinding<Object?> binding in _outgoingServicesBindings) {
      binding.close();
    }
  }
}
