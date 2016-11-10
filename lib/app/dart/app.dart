// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:fidl.internal';

import 'package:lib.fidl.dart/bindings.dart';
import 'package:lib.fidl.dart/core.dart' as core;

import 'package:apps.modular.services.application/application_environment.fidl.dart';
import 'package:apps.modular.services.application/application_launcher.fidl.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';

class ApplicationContext {
  ApplicationContext();

  final ApplicationEnvironmentProxy environment = new ApplicationEnvironmentProxy();
  final ApplicationLauncherProxy launcher = new ApplicationLauncherProxy();
  final ServiceProviderProxy environmentServices = new ServiceProviderProxy();
  final ServiceProviderImpl outgoingServices = new ServiceProviderImpl();

  factory ApplicationContext.fromStartupInfo() {
    final ApplicationContext context = new ApplicationContext();

    final int environmentHandle = MxStartupInfo.takeEnvironment();
    if (environmentHandle != null) {
      final core.Handle handle = new core.Handle(environmentHandle);
      context.environment
        ..ctrl.bind(new InterfaceHandle<ApplicationEnvironment>(new core.Channel(handle), 0))
        ..getApplicationLauncher(context.launcher.ctrl.request())
        ..getServices(context.environmentServices.ctrl.request());
    }

    final int outgoingServicesHandle = MxStartupInfo.takeOutgoingServices();
    if (outgoingServicesHandle != null) {
      final core.Handle handle = new core.Handle(outgoingServicesHandle);
      context.outgoingServices.bind(new InterfaceRequest<ServiceProvider>(new core.Channel(handle)));
    }

    return context;
  }
}

void connectToService(ServiceProvider serviceProvider,
                      ProxyController controller) {
  final String serviceName = controller.serviceName;
  if (serviceName == null)
    return;
  serviceProvider.connectToService(serviceName, controller.request().passChannel());
}

typedef void ServiceConnector(InterfaceRequest request);
typedef void DefaultServiceConnector(String serviceName,
                                     InterfaceRequest request);

class ServiceProviderImpl extends ServiceProvider {
  final ServiceProviderBinding _binding = new ServiceProviderBinding();

  void bind(InterfaceRequest<ServiceProvider> interfaceRequest) {
    _binding.bind(this, interfaceRequest);
  }

  DefaultServiceConnector defaultConnector;

  final Map<String, ServiceConnector> _connectors = new Map<String, ServiceConnector>();

  void addServiceForName(ServiceConnector connector, String serviceName) {
    _connectors[serviceName] = connector;
  }

  @override
  void connectToService(String serviceName, core.Channel channel) {
    final ServiceConnector connector = _connectors[serviceName];
    if (connector != null) {
      connector(new InterfaceRequest(channel));
    } else if (defaultConnector != null) {
      defaultConnector(serviceName, new InterfaceRequest(channel));
    } else {
      channel.close();
    }
  }
}
