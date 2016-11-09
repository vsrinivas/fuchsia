// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:fidl.internal';

import 'package:lib.fidl.dart/core.dart' as core;

import 'package:apps.modular.services.application/application_environment.fidl.dart';
import 'package:apps.modular.services.application/application_launcher.fidl.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';

class ApplicationContext {
  ApplicationContext({
    this.environmentServices,
    this.environment,
    this.launcher,
    this.outgoingServices,
  });

  final ServiceProviderProxy environmentServices;
  final ApplicationEnvironmentProxy environment;
  final ApplicationLauncherProxy launcher;
  final ServiceProviderImpl outgoingServices;

  factory ApplicationContext.fromStartupInfo() {
    ApplicationEnvironmentProxy environment;
    ApplicationLauncherProxy launcher;
    ServiceProviderImpl outgoingServices;

    int environmentHandle = MxStartupInfo.takeEnvironment();
    if (environmentHandle != null) {
      core.Handle handle = new core.Handle(environmentHandle);
      environment = new ApplicationEnvironmentProxy.fromHandle(handle);
      launcher = new ApplicationLauncherProxy.unbound();
      environment.getApplicationLauncher(launcher);
    }

    int outgoingServicesHandle = MxStartupInfo.takeOutgoingServices();
    if (outgoingServicesHandle != null) {
      core.Handle handle = new core.Handle(outgoingServicesHandle);
      ServiceProviderStub stub = new ServiceProviderStub.fromHandle(handle);
      outgoingServices = new ServiceProviderImpl(stub);
    }

    return new ApplicationContext(
      environment: environment,
      launcher: launcher,
      outgoingServices: outgoingServices,
    );
  }
}

void connectToService(ServiceProvider serviceProvider, Proxy proxy) {
  String serviceName = proxy.ctrl.serviceName;
  if (serviceName == null)
    return;
  serviceProvider.connectToService(serviceName, proxy);
}

typedef void ServiceConnector(core.Channel channel);

class ServiceProviderImpl extends ServiceProvider {
  ServiceProviderImpl(this.stub) { _sub.impl = this; }

  final ServiceProviderStub stub;

  ServiceConnector defaultConnector;

  final Map<String, ServiceConnector> _connectors = new Map<String, ServiceConnector>();

  void addServiceForName(ServiceConnector connector, String serviceName) {
    _connectors[serviceName] = connector;
  }

  @override
  void connectToService(String interfaceName, core.Channel channel) {
    ServiceConnector connector = _connectors[serviceName];
    if (connector != null) {
      connector(channel);
    } else if (defaultConnector != null) {
      defaultConnector(channel)
    } else {
      channel.close();
    }
  }
}
