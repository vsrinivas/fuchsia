// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/widgets.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

/// This is used for keeping the reference around.
ModuleImpl _module;

void _log(String msg) {
  print('[Hello World Module] $msg');
}

/// An implementation of the [Module] interface.
class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();
  final LinkProxy _link = new LinkProxy();

  /// Bind an [InterfaceRequest] for a [Module] interface to this object.
  void bind(InterfaceRequest<Module> request) {
    _binding.bind(this, request);
  }

  /// Implementation of the Initialize(ModuleContext story, Link link) method.
  @override
  void initialize(
      InterfaceHandle<ModuleContext> moduleContextHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _log('ModuleImpl::initialize call');

    // NOTE: These story / link proxy variables must not be local variables.
    // When a handle is bound to a proxy and then the proxy variable is garbage
    // collected before the pipe is properly closed or unbound, the app will
    // crash due to the leaked handle.

    _moduleContext.ctrl.bind(moduleContextHandle);
    _moduleContext.getLink(null, _link.ctrl.request());
    _moduleContext.ready();
  }

  /// Implementation of the Stop() => (); method.
  @override
  void stop(void callback()) {
    _log('ModuleImpl.stop()');

    _moduleContext.ctrl.close();
    _link.ctrl.close();

    // Invoke the callback to signal that the clean-up process is done.
    callback();

    _binding.close();
  }
}

/// Entry point for this module.
void main() {
  _log('Module started with ApplicationContext: $_appContext');

  /// Add [ModuleImpl] to this application's outgoing ServiceProvider.
  _appContext.outgoingServices.addServiceForName(
    (request) {
      _log('Received binding request for Module');
      _module = new ModuleImpl()..bind(request);
    },
    Module.serviceName,
  );

  runApp(new Text("Hello, world!"));
}
