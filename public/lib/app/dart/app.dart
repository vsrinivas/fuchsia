// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:fuchsia';
import 'dart:zircon';

import 'package:lib.fidl.dart/bindings.dart';
import 'package:lib.app.fidl/application_environment.fidl.dart';
import 'package:lib.app.fidl/application_launcher.fidl.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';

class ApplicationContext {
  ApplicationContext();

  final ApplicationEnvironmentProxy environment =
      new ApplicationEnvironmentProxy();
  final ApplicationLauncherProxy launcher = new ApplicationLauncherProxy();
  final ServiceProviderProxy environmentServices = new ServiceProviderProxy();
  final ServiceProviderImpl outgoingServices = new ServiceProviderImpl();

  factory ApplicationContext.fromStartupInfo() {
    final ApplicationContext context = new ApplicationContext();

    final Handle environmentHandle = MxStartupInfo.takeEnvironment();
    if (environmentHandle != null) {
      context.environment
        ..ctrl.bind(new InterfaceHandle<ApplicationEnvironment>(
            new Channel(environmentHandle), 0))
        ..getApplicationLauncher(context.launcher.ctrl.request())
        ..getServices(context.environmentServices.ctrl.request());
    }

    final Handle outgoingServicesHandle = MxStartupInfo.takeOutgoingServices();
    if (outgoingServicesHandle != null) {
      context.outgoingServices.bind(
          new InterfaceRequest<ServiceProvider>(new Channel(outgoingServicesHandle)));
    }

    return context;
  }

  void close() {
    environment.ctrl.close();
    launcher.ctrl.close();
    environmentServices.ctrl.close();
    outgoingServices.close();
  }
}

void connectToService(
    ServiceProvider serviceProvider, ProxyController controller) {
  final String serviceName = controller.serviceName;
  if (serviceName == null) return;
  serviceProvider.connectToService(
      serviceName, controller.request().passChannel());
}

InterfaceHandle connectToServiceByName(
    ServiceProvider serviceProvider, String serviceName) {
  final ChannelPair pair = new ChannelPair();
  serviceProvider.connectToService(serviceName, pair.first);
  return new InterfaceHandle(pair.second, 0);
}

typedef void ServiceConnector<T>(InterfaceRequest<T> request);
typedef void DefaultServiceConnector(
    String serviceName, InterfaceRequest request);

class ServiceProviderImpl extends ServiceProvider {
  final ServiceProviderBinding _binding = new ServiceProviderBinding();

  void bind(InterfaceRequest<ServiceProvider> interfaceRequest) {
    _binding.bind(this, interfaceRequest);
  }

  void close() {
    _binding.close();
  }

  DefaultServiceConnector defaultConnector;

  final Map<String, ServiceConnector> _connectors =
      new Map<String, ServiceConnector>();

  void addServiceForName<T>(ServiceConnector<T> connector, String serviceName) {
    _connectors[serviceName] = connector;
  }

  @override
  void connectToService(String serviceName, Channel channel) {
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
