// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:fidl.internal';

import 'package:lib.fidl.dart/core.dart' as core;

import 'package:apps.modular.services.application/application_environment.fidl.dart';
import 'package:apps.modular.services.application/application_launcher.fidl.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';

class ApplicationContext {
  final ServiceProviderProxy environmentServices;
  final ApplicationEnvironmentProxy environment;
  final ApplicationLauncherProxy launcher;

  ApplicationContext({
    this.environmentServices,
    this.environment,
    this.launcher,
  });

  factory ApplicationContext.fromStartupInfo() {
    ApplicationEnvironmentProxy environment;
    ApplicationLauncherProxy launcher;
    if (MxStartupInfo.environment != null) {
      environment = new ApplicationEnvironmentProxy.fromHandle(
            new core.Handle(MxStartupInfo.environment));
      MxStartupInfo.environment = null;
      environment.getApplicationLauncher(launcher);
    }
    // TODO(abarth): Do something useful with MxStartupInfo.outgoingServices. 
    return new ApplicationContext(environment: environment, launcher: launcher);
  }
}

void connectToService(ServiceProvider serviceProvider, Proxy proxy) {
  String serviceName = proxy.ctrl.serviceName;
  if (serviceName == null)
    return;
  serviceProvider.connectToService(serviceName, proxy);
}
