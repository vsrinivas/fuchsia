// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/material.dart';

import 'dummy_photo_storage.dart';
import 'home.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

ModuleImpl _moduleImpl;

void _log(String msg) {
  print('[Photos Flutter Example] $msg');
}

/// An implementation of the [Module] interface.
class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  /// Bind an [InterfaceRequest] for a [Module] interface to this object.
  void bind(InterfaceRequest<Module> request) {
    _binding.bind(this, request);
  }

  /// Implementation of the Initialize(Story story, Link link) method.
  @override
  void initialize(
      InterfaceHandle<ModuleContext> moduleContextHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _log('ModuleImpl::initialize call');
  }

  /// Implementation of the Stop() => (); method.
  @override
  void stop(void callback()) {
    _log('ModuleImpl.stop()');

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
      _moduleImpl = new ModuleImpl();
      _moduleImpl.bind(request);
    },
    Module.serviceName,
  );
  final storage = new DummyPhotoStorage();
  runApp(new MaterialApp(
    title: 'Photos Example',
    home: new Home(storage: storage),
  ));
}
