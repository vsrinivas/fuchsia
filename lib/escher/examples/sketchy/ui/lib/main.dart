// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_controller.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'src/app.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

EscherDemoModule _module;

/// An implementation of the [Module] interface.
class EscherDemoModule extends Module {
  final ModuleBinding _binding = new ModuleBinding();
  final ModuleContextProxy _moduleContext = new ModuleContextProxy();

  /// Bind an [InterfaceRequest] for a [Module] interface to this object.
  void bind(InterfaceRequest<Module> request) {
    _binding.bind(this, request);
  }

  @override
  void initialize(
      InterfaceHandle<ModuleContext> moduleContextHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _moduleContext.ctrl.bind(moduleContextHandle);
    incomingServices?.close();
    outgoingServices?.close();
  }

  @override
  void stop(void callback()) {
    _moduleContext.ctrl.close();

    callback();
  }
}

/// Entry point for this module.
void main() {
  _appContext.outgoingServices.addServiceForName(
    (InterfaceRequest<Module> request) {
      if (_module == null) {
        _module = new EscherDemoModule();
      }

      _module.bind(request);
    },
    Module.serviceName,
  );

  runApp(new App((String appName) {
    // If we would retain the module controller for the child module, we
    // could later stop it.
    InterfacePair<ModuleController> moduleControllerPair =
        new InterfacePair<ModuleController>();

    InterfacePair<ViewOwner> viewOwnerPair = new InterfacePair<ViewOwner>();

    ServiceProviderProxy incomingServices = new ServiceProviderProxy();

    _module._moduleContext.startModule(
        'Escher Demo',
        'file:///system/apps/$appName',
        null, // pass our default link to the child
        null, // outgoing services
        incomingServices.ctrl.request(),
        moduleControllerPair.passRequest(),
        viewOwnerPair.passRequest());

    return incomingServices;
  }));
}
