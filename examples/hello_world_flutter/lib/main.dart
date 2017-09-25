// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:isolate';

import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';
import 'package:lib.story.fidl/link.fidl.dart';
import 'package:lib.lifecycle.fidl/lifecycle.fidl.dart';
import 'package:lib.module.fidl/module.fidl.dart';
import 'package:lib.module.fidl/module_context.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/widgets.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

/// This is used for keeping the reference around.
ModuleImpl _module = new ModuleImpl();

void _log(String msg) {
  print('[Hello World Module] $msg');
}

/// An implementation of the [Module] interface.
class ModuleImpl implements Module, Lifecycle {
  final ModuleBinding _moduleBinding = new ModuleBinding();
  final LifecycleBinding _lifecycleBinding = new LifecycleBinding();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();
  final LinkProxy _link = new LinkProxy();

  /// Bind an [InterfaceRequest] for a [Module] interface to this object.
  void bindModule(InterfaceRequest<Module> request) {
    _moduleBinding.bind(this, request);
  }

  /// Bind an [InterfaceRequest] for a [Lifecycle] interface to this object.
  void bindLifecycle(InterfaceRequest<Lifecycle> request) {
    _lifecycleBinding.bind(this, request);
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

  @override
  void terminate() {
    _log('Lifecycle.terminate()');

    _moduleContext.ctrl.close();
    _link.ctrl.close();

    _moduleBinding.close();
    _lifecycleBinding.close();
    Isolate.current.kill();
  }
}

/// Entry point for this module.
void main() {
  _log('Module started with ApplicationContext: $_appContext');

  /// Add [ModuleImpl] to this application's outgoing ServiceProvider.
  _appContext.outgoingServices
    ..addServiceForName(
      (InterfaceRequest<Module> request) {
        _log('Received binding request for Module');
        _module.bindModule(request);
      },
      Module.serviceName,
    )
    ..addServiceForName(
      (InterfaceRequest<Lifecycle> request) {
        _module.bindLifecycle(request);
      },
      Lifecycle.serviceName,
    );

  runApp(new Text("Hello, world!"));
}
