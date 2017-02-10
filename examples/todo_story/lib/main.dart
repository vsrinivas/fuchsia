// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

import "./data.dart";

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

void _log(String msg) {
  print('[Todo Story Example] $msg');
}

/// An implementation of the [Module] interface.
class TodoModule extends Module {
  final ModuleBinding _binding = new ModuleBinding();
  final Completer<LinkProxy> _linkCompleter = new Completer<LinkProxy>();
  TodoListData data;

  TodoModule() {
    data = new TodoListData(_linkCompleter.future);
  }

  /// Bind an [InterfaceRequest] for a [Module] interface to this object.
  void bind(InterfaceRequest<Module> request) {
    _binding.bind(this, request);
  }

  /// Implementation of the Initialize(Story story, Link link) method.
  @override
  void initialize(
      InterfaceHandle<Story> storyHandle,
      InterfaceHandle<Link> linkHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _log('TodoModule::initialize()');

    LinkProxy link = new LinkProxy();
    link.ctrl.bind(linkHandle);
    _linkCompleter.complete(link);

    // Do something with the story and link services.
  }

  /// Implementation of the Stop() => (); method.
  @override
  void stop(void callback()) {
    _log('TodoModule::stop()');

    // Do some clean up here.

    // Invoke the callback to signal that the clean-up process is done.
    callback();
  }
}

class TodoList extends StatefulWidget {
  TodoList(TodoModule this._module);

  final TodoModule _module;

  @override
  TodoListState createState() => new TodoListState(_module.data);
}

class TodoListState extends State<TodoList> {
  final List<String> _todoItems = [
    "Save the world",
    "Write unit tests",
    "Sleep",
    "Finish this app"
  ];


  TodoListState(TodoListData this._data) {
    _data.setCallback((List<String> items) {
      setState(() {
        _items.clear();
        _items.addAll(items);
      });
    });

    _timer = new Timer.periodic(const Duration(seconds: 2), (Timer timer) {
      List<String> newItems = new List.from(_items);
      if (_random.nextInt(100) > 50) {
        newItems.add(_todoItems[_random.nextInt(_todoItems.length)]);
      } else {
        if (newItems.isNotEmpty) {
          newItems.removeAt(_random.nextInt(newItems.length));
        }
      }
      _data.setNewList(newItems);
    });
  }

  final Random _random = new Random();
  Timer _timer;

  final List<String> _items = new List<String>();
  TodoListData _data;

  @override
  Widget build(BuildContext context) {
    String res = "";
    for (String item in _items) {
      res += " - $item\n";
    }
    return new Text(res);
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
    home: new TodoList(module),
    theme: new ThemeData(primarySwatch: Colors.blue),
  ));
}
