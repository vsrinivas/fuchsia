// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.maxwell.services.context/context_publisher.fidl.dart';
import 'package:apps.maxwell.services.context/context_reader.fidl.dart';
import 'package:apps.maxwell.services.user/intelligence_services.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

/// The context topic for "focal entities" for the current story.
const String _kCurrentFocalEntitiesTopic = '/inferred/focal_entities';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();
final TextEditingController _controller = new TextEditingController();

/// This is used for keeping the reference around.
ModuleImpl _module;

void _log(String msg) {
  print('[Basic Text Reporter Module] $msg');
}

// A listener for finding updated focal entities and applying UI treatment to
// them.
class ContextListenerForTopicsImpl extends ContextListenerForTopics {
  final ContextListenerForTopicsBinding _binding = new ContextListenerForTopicsBinding();

  /// Constructor
  ContextListenerForTopicsImpl();

  /// Gets the [InterfaceHandle]
  /// The returned handle should only be used once.
  InterfaceHandle<ContextListenerForTopics> getHandle() => _binding.wrap(this);

  @override
  Future<Null> onUpdate(ContextUpdateForTopics result) async {
    if (!result.values.containsKey(_kCurrentFocalEntitiesTopic)) {
      return;
    }

    List<dynamic> data =
        JSON.decode(result.values[_kCurrentFocalEntitiesTopic]);

    String outputText = "";
    int lastEnd = 0;
    final String allControllerText = _controller.text;
    // Without this, selection might be overwritten.
    final TextSelection oldSelection = _controller.selection;
    for (dynamic entity in data) {
      if (!(entity is Map<String, dynamic>) &&
          entity.containsKey('start') &&
          entity.containsKey('end')) {
        final int start = entity['start'];
        final int end = entity['end'];
        outputText += allControllerText.substring(lastEnd, start).toLowerCase();
        outputText += allControllerText.substring(start, end).toUpperCase();
        lastEnd = end;
      }
    }
    outputText +=
        allControllerText.substring(lastEnd, allControllerText.length);
    if (outputText.length != allControllerText.length) {
      _log('LENGTH MISMATCH');
    } else {
      _controller.text = outputText;
      _controller.selection = oldSelection;
    }
  }
}

/// An implementation of the [Module] interface.
class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();
  final LinkProxy _link = new LinkProxy();

  final ContextPublisherProxy _publisher = new ContextPublisherProxy();
  final IntelligenceServicesProxy _intelligenceServices =
      new IntelligenceServicesProxy();

  final ContextReaderProxy _contextReader = new ContextReaderProxy();
  ContextListenerForTopicsImpl _contextListenerImpl;

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

    // Listen to updates from the context service.
    _intelligenceServices.getContextReader(_contextReader.ctrl.request());
    _contextListenerImpl = new ContextListenerForTopicsImpl();
    ContextQueryForTopics query = new ContextQueryForTopics.init(
        <String>[_kCurrentFocalEntitiesTopic], null /* filters */);
    _contextReader.subscribeToTopics(query, _contextListenerImpl.getHandle());

    // Indicate readiness
    _moduleContext.ready();
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
        <String, String>{
          'text': text,
        },
      ),
    );
  }

  void publishSelection(int start, int end) {
    _publisher.publish(
      'raw/text_selection',
      JSON.encode(
        <String, int>{
          'start': start,
          'end': end,
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
    int selectionStart = _controller.selection.start;
    int selectionEnd = _controller.selection.end;
    _module.publishText(currentText);
    _module.publishSelection(selectionStart, selectionEnd);
  });

  runApp(new MaterialApp(
    title: "Basic Text Reporter",
    home: new Scaffold(
      appBar: new AppBar(
        title: new Text("Basic Text Reporter"),
      ),
      body: new Container(
        child: new TextField(
          controller: _controller,
          decoration: new InputDecoration(
              hintText: 'Type something, selectable entities will become' +
                  ' ALL CAPS'),
        ),
      ),
    ),
  ));
}
