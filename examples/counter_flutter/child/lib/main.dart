// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:flutter/material.dart';
import 'package:lib.fidl.dart/bindings.dart';

final ApplicationContext _context = new ApplicationContext.fromStartupInfo();

final GlobalKey<_HomeScreenState> _homeKey = new GlobalKey<_HomeScreenState>();

final String _kDocRoot = 'counters';
final String _kDocId = 'counter-doc-id';
final String _kCounterValueKey = "http://schema.domokit.org/counter";
final String _kPingPongPacketType = 'http://schema.domokit.org/PingPongPacket';
final String _kAtTypeKey = '@type';

ModuleImpl _module;

void _log(String msg) {
  print('[Counter Child] $msg');
}

class LinkWatcherImpl extends LinkWatcher {
  final LinkWatcherBinding _binding = new LinkWatcherBinding();

  /// Gets the [InterfaceHandle] for this [LinkWatcher] implementation.
  ///
  /// The returned handle should only be used once.
  InterfaceHandle<LinkWatcher> getHandle() => _binding.wrap(this);

  /// Closes the binding.
  void close() {
    if (_binding.isBound) {
      _binding.close();
    }
  }

  /// A callback called whenever the associated [Link] has new changes.
  @override
  void notify(String json) {
    _log('LinkWatcherImpl.notify()');
    _log('Link data: ${json}');
    dynamic doc = JSON.decode(json);
    if (doc is Map &&
        doc[_kDocRoot] is Map &&
        doc[_kDocRoot][_kDocId] is Map &&
        doc[_kDocRoot][_kDocId][_kCounterValueKey] is int) {
      _homeKey.currentState
          ?.updateValue(doc[_kDocRoot][_kDocId][_kCounterValueKey]);
    }
  }
}

class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  final LinkProxy _link = new LinkProxy();
  final LinkWatcherImpl _linkWatcher = new LinkWatcherImpl();

  final List<String> _jsonPath = <String>[_kDocRoot, _kDocId];

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
    _log('ModuleImpl.initialize()');

    // Bind the provided handles to our proxy objects.
    _link.ctrl.bind(linkHandle);

    // Register the link watcher.
    _link.watchAll(_linkWatcher.getHandle());

    _initValue(42);
  }

  @override
  void stop(void callback()) {
    _log('ModuleImpl.stop()');

    // Do some clean up here.
    _linkWatcher.close();
    _link.ctrl.close();

    // Invoke the callback to signal that the clean-up process is done.
    callback();
  }

  void _initValue(int newValue) {
    _link.set(
        _jsonPath,
        JSON.encode(<String, dynamic>{
          _kAtTypeKey: _kPingPongPacketType,
          _kCounterValueKey: newValue,
        }));
  }

  void _setValue(int newValue) {
    // Update just the value of interest. Don't overwrite other members.
    _link.updateObject(
        _jsonPath, JSON.encode(<String, dynamic>{_kCounterValueKey: newValue}));
  }
}

class _HomeScreen extends StatefulWidget {
  _HomeScreen({Key key}) : super(key: key);

  @override
  _HomeScreenState createState() => new _HomeScreenState();
}

class _HomeScreenState extends State<_HomeScreen> {
  int _linkValue = 0;

  int get _currentValue {
    return _linkValue;
  }

  void updateValue(int value) {
    setState(() => _linkValue = value);
  }

  @override
  Widget build(BuildContext context) {
    List<Widget> children = <Widget>[
      new Text('I am the child module!'),
      new Text('Current Value: $_currentValue'),
      new Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: <Widget>[
          new RaisedButton(
            onPressed: _handleIncrease,
            child: new Text('Increase'),
          ),
          new RaisedButton(
            onPressed: _handleDecrease,
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

  void _handleIncrease() {
    _module._setValue(_currentValue + 1);
  }

  void _handleDecrease() {
    _module._setValue(_currentValue - 1);
  }
}

/// Main entry point to the example child module.
void main() {
  _log('main()');

  _context.outgoingServices.addServiceForName(
    (InterfaceRequest<Module> request) {
      _log('Service request for Module');
      _module = new ModuleImpl()..bind(request);
    },
    Module.serviceName,
  );

  runApp(new MaterialApp(
    title: 'Counter Child',
    home: new _HomeScreen(key: _homeKey),
    theme: new ThemeData(primarySwatch: Colors.blue),
    debugShowCheckedModeBanner: false,
  ));

  _log('main() exit');
}
