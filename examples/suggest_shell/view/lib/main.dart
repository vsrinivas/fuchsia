// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:isolate';

import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';
import 'package:lib.story.fidl/link.fidl.dart';
import 'package:lib.lifecycle.fidl/lifecycle.fidl.dart';
import 'package:lib.module.fidl/module.fidl.dart';
import 'package:lib.module.fidl/module_context.fidl.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

final String _kValue = "http://schema.domokit.org/suggestion";

void _log(String msg) {
  print('[SuggestShell View] $msg');
}

typedef void _ValueCallback(String value);
typedef void _UpdateCallback();

// Being a Module is the way for an app to share context with other apps
// and form a Story.
class _Module implements Module, Lifecycle, LinkWatcher {
  _Module(this._valueCallback);

  final _ValueCallback _valueCallback;

  final ModuleBinding _moduleBinding = new ModuleBinding();
  final LifecycleBinding _lifecycleBinding = new LifecycleBinding();
  final LinkWatcherBinding _linkWatcherBinding = new LinkWatcherBinding();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();
  final LinkProxy _link = new LinkProxy();

  final List<String> _jsonPath = <String>[_kValue];

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
    _moduleContext.ctrl.bind(moduleContextHandle);
    _moduleContext.getLink(null, _link.ctrl.request());
    _link.watchAll(_linkWatcherBinding.wrap(this));
  }

  /// |Lifecycle|
  @override
  void terminate() {
    _linkWatcherBinding.close();
    _link.ctrl.close();
    _moduleBinding.close();
    _lifecycleBinding.close();
    Isolate.current.kill();
  }

  /// |LinkWatcher|
  @override
  void notify(String json) {
    dynamic doc = JSON.decode(json);
    if (doc is Map && doc[_kValue] is String) {
      _valueCallback(doc[_kValue]);
    }
  }

  // API below is exposed to _AppState. In addition, _AppState uses the
  // constructor argument to get notifications.
  void setValue(String newValue) => _link.set(_jsonPath, JSON.encode(newValue));
}

// Persistent external shared state of the app. Connected to the Story
// through a Link with a Module.
class _AppState {
  _AppState(this._context) {
    _module = new _Module(_updateValue);
    _context.outgoingServices
      ..addServiceForName((InterfaceRequest<Module> request) {
        _log('Service request for Module');
        _module.bindModule(request);
      }, Module.serviceName)
      ..addServiceForName((InterfaceRequest<Lifecycle> request) {
        _module.bindLifecycle(request);
      }, Lifecycle.serviceName);
  }

  // NOTE(mesch): _context is a constructor argument and only used
  // there. We keep it around, however, to prevent it from getting
  // garbage collected, as it holds on to fidl objects whose existence
  // is meaningful.
  final ApplicationContext _context;

  // This is the persistent external application state, which is also
  // stored in the Link of the Module. Everything else in this class
  // orchestrates sources for updates of this state or ways to access
  // this state.
  String _value = "";

  // Watchers to be notified of state changes.
  final List<_UpdateCallback> _watch = <_UpdateCallback>[];

  // The application is connected to a Story as a Module.
  _Module _module;

  // Called by the Module with updates for the state value.
  void _updateValue(String newValue) => _notify(() => _value = newValue);

  // Invokes a function that's supposed to change the state and notifies
  // all watchers about it.
  void _notify(Function change) {
    change();
    _watch.forEach((f) => f());
  }

  // API below is exposed to the view state. It exposes the current
  // state value, to register callbacks for changes of it, and a way to
  // send changes to the state value. The state value change is
  // forwarded to the Module, and state changes are received from
  // watching the Module.
  String get value => _value;
  void sendValue(String value) => _module.setValue(value);
  void watch(_UpdateCallback callback) => _watch.add(callback);
}

// The view state attached to the flutter view.
class _HomeScreenState extends State<_HomeScreen> {
  _HomeScreenState(this._state) {
    _state.watch(() => setState(() {}));
  }

  // The persistent external app state.
  final _AppState _state;

  // The transient view state, which is not sent to the model and
  // persisted and shared until '<<' (out hack replacement of enter) is
  // input.
  String _value = '';

  @override
  Widget build(BuildContext context) {
    // HACK(mesch): There is no text input widget in flutter that works
    // in fuchsia right now. The workaround is to listen to raw keyboard
    // events and make sense of them as much as possible.
    return new RawKeyboardListener(
        focusNode: new FocusNode(),
        onKey: _onKey,
        child: new Material(
            color: Colors.blue[200],
            child: new Column(children: [
              new Text('[type and terminate input with <<]'),
              new Text('Active input: ${_state.value}'),
              new Text('Next input: $_value')
            ])));
  }

  void _onKey(RawKeyEvent e) {
    // HACK(mesch): Enter key presses as well as other control keys all
    // arrive as codePoint 0, so we don't use enter to finish input. We
    // just ignore all such key presses.
    // ignore: undefined_getter
    if (e.runtimeType == RawKeyDownEvent && e.data.codePoint != 0) {
      setState(() {
        // ignore: undefined_getter
        _value += new String.fromCharCode(e.data.codePoint);
        if (_value.endsWith('<<')) {
          _state.sendValue(_value.substring(0, _value.length - 2));
          _value = '';
        }
      });
    }
  }
}

// The top level stateful widget of the application view. It holds on to
// the app state through an instance of the view state. The view state
// instance can change throughout the life of the application (although
// here it doesn't), but the app state instance stays the same for the
// whole life of the app instance.
class _HomeScreen extends StatefulWidget {
  _HomeScreen({Key key, _AppState state})
      : _state = state,
        super(key: key);

  final _AppState _state;

  @override
  _HomeScreenState createState() {
    return new _HomeScreenState(_state);
  }
}

void main() {
  final _AppState state =
      new _AppState(new ApplicationContext.fromStartupInfo());

  runApp(new MaterialApp(
    title: 'Input',
    home: new _HomeScreen(state: state),
    theme: new ThemeData(primarySwatch: Colors.blue),
    debugShowCheckedModeBanner: true,
  ));
}
