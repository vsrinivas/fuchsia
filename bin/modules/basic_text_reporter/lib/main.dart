// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.maxwell.services.context/context_publisher.fidl.dart';
import 'package:apps.maxwell.services.user/intelligence_services.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();
final TextEditingController _controller = new TextEditingController();

/// This is used for keeping the reference around.
ModuleImpl _module;

void _log(String msg) {
  print('[Basic Text Reporter Module] $msg');
}

/// An implementation of the [Module] interface.
class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();
  final LinkProxy _link = new LinkProxy();

  final ContextPublisherProxy _publisher = new ContextPublisherProxy();
  final IntelligenceServicesProxy _intelligenceServices =
    new IntelligenceServicesProxy();


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

    // Do something with the story and link services.
    _moduleContext
        .getIntelligenceServices(_intelligenceServices.ctrl.request());
    _intelligenceServices.getContextPublisher(_publisher.ctrl.request());
  }

  /// Implementation of the Stop() => (); method.
  @override
  void stop(void callback()) {
    _log('ModuleImpl::stop call');

    // Do some clean up here.
    _moduleContext.ctrl.close();
    _link.ctrl.close();

    // Invoke the callback to signal that the clean-up process is done.
    callback();
  }

  void publishText(String text) {
    _publisher.publish(
      'raw/text',
      JSON.encode(
        <String, String> {
          'text' : text,
        },
      ),
    );
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

  _controller.addListener(() {
    String currentText = _controller.text;
//    _log("Current text: $currentText");
    _module.publishText(currentText);
  });

  runApp(
    new MaterialApp(
        title : "Basic Text Reporter",
        home : new Scaffold(
            appBar : new AppBar(
              title : new Text("Basic Text Reporter"),
            ),
            body : new TextField(
                controller : _controller,
                decoration : new InputDecoration(
                    hintText : 'Type something',
                ),
            ),
        )
    )
  );
}
