// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is very similar (although not identical) to
// parent/lib/main.dart. For comments that explain the purposes of the
// classes and their members, see the comments there.

import 'dart:convert';
import 'dart:isolate';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.lifecycle/lifecycle.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:flutter/material.dart';
import 'package:lib.fidl.dart/bindings.dart';

final String _kCounterValueKey = "http://schema.domokit.org/counter";

void _log(String msg) {
  print('[Counter Child] $msg');
}

typedef void _ValueCallback(int);
typedef void _UpdateCallback();

class _ChildCounterModule implements Module, Lifecycle, LinkWatcher {
  _ChildCounterModule(this._valueCallback);

  final _ValueCallback _valueCallback;

  final ModuleBinding _moduleBinding = new ModuleBinding();
  final LifecycleBinding _lifecycleBinding = new LifecycleBinding();
  final LinkWatcherBinding _linkWatcherBinding = new LinkWatcherBinding();

  final LinkProxy _link = new LinkProxy();

  final List<String> _jsonPath = <String>[_kCounterValueKey];

  void bindModule(InterfaceRequest<Module> request) {
    _moduleBinding.bind(this, request);
  }
  
  void bindLifecycle(InterfaceRequest<Lifecycle> request) {
    _lifecycleBinding.bind(this, request);
  }

  /// |Module|
  @override
  void initialize(
      InterfaceHandle<ModuleContext> moduleContextHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _log('_ChildCounterModule.initialize()');

    ModuleContextProxy moduleContext = new ModuleContextProxy()
      ..ctrl.bind(moduleContextHandle);
    moduleContext.getLink(null, _link.ctrl.request());
    moduleContext.ctrl.close();

    _link.watchAll(_linkWatcherBinding.wrap(this));
  }

  /// |Lifecycle|
  @override
  void terminate() {
    _log('_ChildCounterModule.terminate()');

    _linkWatcherBinding.close();
    _link.ctrl.close();
    _moduleBinding.close();
    _lifecycleBinding.close();
    Isolate.current.kill();
  }

  /// |LinkWatcher|
  @override
  void notify(String json) {
    _log('LinkWatcherImpl.notify()');
    _log('Link data: $json');
    dynamic doc = JSON.decode(json);
    if (doc is Map && doc[_kCounterValueKey] is int) {
      _valueCallback(doc[_kCounterValueKey]);
    }
  }

  // API below is exposed to _AppState. In addition, _AppState uses the
  // constructor argument to get notifications.
  void setValue(int newValue) => _link.set(_jsonPath, JSON.encode(newValue));
}

class _AppState {
  _AppState(this._context) {
    _module = new _ChildCounterModule(_updateValue);
    _context.outgoingServices
    ..addServiceForName(
        (InterfaceRequest<Module> request) {
      _log('Service request for Module');
      _module.bindModule(request);
    }, Module.serviceName)
    ..addServiceForName(
        (InterfaceRequest<Lifecycle> request) {
      _log('Service request for Lifecycle');
      _module.bindLifecycle(request);
    }, Lifecycle.serviceName);
  }

  // NOTE(mesch): _context is a constructor argument and only used
  // there. We keep it around, however, to prevent it from getting
  // garbage collected, as it holds on to fidl objects whose existence
  // is meaningful.
  final ApplicationContext _context;

  // This is the application state. Everything else in this class
  // orchestrates sources for updates of this state or ways to access
  // this state.
  int _value = 0;

  // Watchers to be notified of state changes.
  final List<_UpdateCallback> _watch = <_UpdateCallback>[];

  // The application is connected to a Story as a Module.
  _ChildCounterModule _module;

  void _updateValue(int newValue) => _notify(() => _value = newValue);

  void _notify(Function change) {
    change();
    _watch.forEach((f) => f());
  }

  // API below is exposed to the view state.
  int get value => _value;
  void increment() => _module.setValue(_value + 1);
  void decrement() => _module.setValue(_value - 1);
  void watch(_UpdateCallback callback) => _watch.add(callback);
}

class _HomeScreen extends StatefulWidget {
  _HomeScreen({Key key, _AppState state})
      : _state = state,
        super(key: key);

  final _AppState _state;

  @override
  _HomeScreenState createState() {
    _log("HomeScreen.createState()");
    return new _HomeScreenState(_state);
  }
}

class _HomeScreenState extends State<_HomeScreen> {
  _HomeScreenState(this._state) {
    _state.watch(() => setState(() {}));
  }

  final _AppState _state;

  @override
  Widget build(BuildContext context) {
    final List<Widget> children = <Widget>[
      new Text('I am the child module!'),
      new Text('Current Value: ${_state.value}'),
      new Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: <Widget>[
          new RaisedButton(
            onPressed: _state.increment,
            child: new Text('Increase'),
          ),
          new RaisedButton(
            onPressed: _state.decrement,
            child: new Text('Decrease'),
          ),
        ],
      ),
    ];

    return new Material(
      color: Colors.blue[200],
      child: new Container(
        child: new Column(children: children),
      ),
    );
  }
}

void main() {
  _log('main()');

  final _AppState state =
      new _AppState(new ApplicationContext.fromStartupInfo());

  runApp(new MaterialApp(
    title: 'Counter Child',
    home: new _HomeScreen(state: state),
    theme: new ThemeData(primarySwatch: Colors.blue),
    debugShowCheckedModeBanner: false,
  ));

  _log('main() exit');
}
