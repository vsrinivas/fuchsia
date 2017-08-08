// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

import "./data.dart";
import "./view.dart";

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

void _log(String msg) {
  print('[Todo Story Example] $msg');
}

/// An implementation of the [Module] interface.
class TodoModule extends Module {
  final ModuleBinding _binding = new ModuleBinding();
  final Completer<LinkProxy> _linkCompleter = new Completer<LinkProxy>();
  LinkConnector linkConnector;

  TodoModule() {
    linkConnector = new LinkConnector(_linkCompleter.future);
  }

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
    _log('TodoModule::initialize()');

    LinkProxy link = new LinkProxy();
    ModuleContextProxy moduleContext = new ModuleContextProxy()
      ..ctrl.bind(moduleContextHandle);
    moduleContext.getLink(null, link.ctrl.request());
    _linkCompleter.complete(link);
  }

  /// Implementation of the Stop() => (); method.
  @override
  void stop(void callback()) {
    _log('TodoModule.stop()');

    // Invoke the callback to signal that the clean-up process is done.
    callback();

    _binding.close();
  }
}

void main() {
  _log('Module started with ApplicationContext: $_appContext');

  final module = new TodoModule();

  _appContext.outgoingServices.addServiceForName(
    (request) {
      _log('Received binding request for Module');
      module.bind(request);
    },
    Module.serviceName,
  );

  runApp(new MaterialApp(
    title: 'Todo (Story)',
    home: new TodoListView(module.linkConnector),
    theme: new ThemeData(primarySwatch: Colors.blue),
  ));
}
