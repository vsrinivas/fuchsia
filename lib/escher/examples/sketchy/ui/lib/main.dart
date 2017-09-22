// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:isolate';

import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';
import 'package:lib.lifecycle.fidl/lifecycle.fidl.dart';
import 'package:lib.module.fidl/module.fidl.dart';
import 'package:lib.module.fidl/module_controller.fidl.dart';
import 'package:lib.module.fidl/module_context.fidl.dart';
import 'package:lib.ui.flutter/child_view.dart';
import 'package:lib.ui.views.fidl/view_token.fidl.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'src/app.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

EscherDemoModule _module = new EscherDemoModule();

/// An implementation of the [Module] interface.
class EscherDemoModule implements Module, Lifecycle {
  final ModuleBinding _moduleBinding = new ModuleBinding();
  final LifecycleBinding _lifecycleBinding = new LifecycleBinding();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();

  /// Bind an [InterfaceRequest] for a [Module] interface to this object.
  void bindModule(InterfaceRequest<Module> request) {
    _moduleBinding.bind(this, request);
  }

  /// Bind an [InterfaceRequest] for a [Lifecycle] interface to this object.
  void bindLifecycle(InterfaceRequest<Lifecycle> request) {
    _lifecycleBinding.bind(this, request);
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
  void terminate() {
    _moduleContext.ctrl.close();
    _moduleBinding.close();
    _lifecycleBinding.close();
    Isolate.current.kill();
  }
}

/// Entry point for this module.
void main() {
  _appContext.outgoingServices
  ..addServiceForName(
    (InterfaceRequest<Module> request) {
      _module.bindModule(request);
    },
    Module.serviceName,
  )..addServiceForName(
    (InterfaceRequest<Lifecycle> request) {
      _module.bindLifecycle(request);
    },
    Lifecycle.serviceName,
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
