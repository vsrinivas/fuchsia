// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:isolate';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.lifecycle/lifecycle.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_controller.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:lib.ui.flutter/child_view.dart';
import 'package:lib.ui.views.fidl/view_token.fidl.dart';
import 'package:flutter/material.dart';
import 'package:lib.fidl.dart/bindings.dart';

final String _kCounterValueKey = "http://schema.domokit.org/counter";
final String _kJsonSchema = '''
{
  "\$schema": "http://json-schema.org/draft-04/schema#",
  "type": "object",
  "properties": {
     "$_kCounterValueKey": {
        "type": "integer"
     }
   },
   "additionalProperties" : false,
   "required": [
      "$_kCounterValueKey"
   ]
}
''';

void _log(String msg) {
  print('[Counter Parent] $msg');
}

typedef void _ValueCallback(int);
typedef void _UpdateCallback();
typedef void _ChildViewCallback(ChildViewConnection connection);

// This module produces two things: A connection to the view of its
// child module, and updates of the value shared with that child module
// through a Link.
class _ParentCounterModule implements Module, Lifecycle, LinkWatcher {
  _ParentCounterModule(this._valueCallback, this._childViewCallback);

  final _ValueCallback _valueCallback;
  final _ChildViewCallback _childViewCallback;

  final ModuleBinding _moduleBinding = new ModuleBinding();
  final LifecycleBinding _lifecycleBinding = new LifecycleBinding();
  final LinkWatcherBinding _linkWatcherBinding = new LinkWatcherBinding();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();
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
    _log('_ParentCounterModule.initialize()');

    // A module is initialized with a ModuleContext and a Link.
    _moduleContext.ctrl.bind(moduleContextHandle);
    _moduleContext.getLink(null, _link.ctrl.request());

    // On the link, we can declare that values stored in the link adhere
    // to a schema. If an update violates the schema, this only created
    // debug log output.
    _link.setSchema(_kJsonSchema);

    // If the value in the link changes, we notice this.
    _link.watchAll(_linkWatcherBinding.wrap(this));

    // If we would retain the module controller for the child module, we
    // could later stop it.
    InterfacePair<ModuleController> moduleControllerPair =
        new InterfacePair<ModuleController>();

    InterfacePair<ViewOwner> viewOwnerPair = new InterfacePair<ViewOwner>();

    // Start the child module.
    _moduleContext.startModule(
        'child',
        'file:///system/apps/example_flutter_counter_child',
        null, // Pass our default link to our child.
        null,
        null,
        moduleControllerPair.passRequest(),
        viewOwnerPair.passRequest());

    _childViewCallback(new ChildViewConnection(viewOwnerPair.passHandle()));
  }

  /// |Lifecycle|
  @override
  void terminate() {
    _log('_ParentCounterModule.terminate()');

    _linkWatcherBinding.close();
    _link.ctrl.close();
    _moduleContext.ctrl.close();
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
  // constructor arguments to get notifications.
  void done() => _moduleContext.done();
  void setValue(int newValue) => _link.set(_jsonPath, JSON.encode(newValue));
}

class _AppState {
  _AppState(this._context) {
    _module = new _ParentCounterModule(_updateValue, _updateChildView);
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

  // This module explicitly hosts another module. This is a connection
  // to its view.
  ChildViewConnection _childViewConnection;

  // Watchers to be notified of state changes.
  final List<_UpdateCallback> _watch = <_UpdateCallback>[];

  // The application is connected to a Story as a Module.
  _ParentCounterModule _module;

  void _updateValue(int newValue) => _notify(() => _value = newValue);
  void _updateChildView(ChildViewConnection v) =>
      _notify(() => _childViewConnection = v);

  void _notify(Function change) {
    change();
    _watch.forEach((f) => f());
  }

  // API below is exposed to the view state.
  int get value => _value;
  ChildViewConnection get childViewConnection => _childViewConnection;
  void increment() => _module.setValue(_value + 1);
  void decrement() => _module.setValue(_value - 1);
  void done() => _module.done();

  // This allows the view to update when the value changes through the link.
  void watch(_UpdateCallback callback) => _watch.add(callback);
}

class _HomeScreen extends StatefulWidget {
  _HomeScreen({Key key, _AppState state})
      : _state = state,
        super(key: key) {
    _log("HomeScreen()");
  }

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
    _log("HomeScreenState::build()");
    final List<Widget> children = <Widget>[
      new Text('I am the parent module!'),
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
          new RaisedButton(
            onPressed: _state.done,
            child: new Text('Stop'),
          ),
        ],
      ),
    ];

    if (_state.childViewConnection != null) {
      children.add(new Expanded(
        flex: 1,
        child: new ChildView(connection: _state.childViewConnection),
      ));
    }

    return new Material(
      color: Colors.orange[200],
      child: new Container(
        child: new Column(children: children),
      ),
    );
  }
}

/// Main entry point to the example parent module.
void main() {
  _log('main()');

  // NOTE(mesch): Application state is hung off the state of a top level
  // stateful widget. It, however, is deliberately not directly the
  // state of this widget. The reason becomes clear if there were
  // multiple top level widgets, e.g. when the MaterialApp would
  // actually have multiple routes, each with a top level stateful
  // widget. In that case, the application state would be *shared*
  // between all those widgets, which a widget state instance would not
  // be.
  final _AppState state =
      new _AppState(new ApplicationContext.fromStartupInfo());

  runApp(new MaterialApp(
    title: 'Counter Parent',
    home: new _HomeScreen(state: state),
    theme: new ThemeData(primarySwatch: Colors.orange),
    debugShowCheckedModeBanner: true,
  ));

  _log('main() exit');
}
