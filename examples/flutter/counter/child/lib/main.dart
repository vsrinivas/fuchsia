// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';
import 'package:apps.modular.services.document_store/document.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:flutter/material.dart';
import 'package:lib.fidl.dart/bindings.dart';

final ApplicationContext _context = new ApplicationContext.fromStartupInfo();

final GlobalKey<_HomeScreenState> _homeKey = new GlobalKey<_HomeScreenState>();

final String _kDocId = 'counter-doc-id';
final String _kCounterValueKey = 'counter-key';

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

  /// A callback called whenever the associated [Link] has new changes.
  @override
  void notify(Map<String, Document> docs) {
    _log('Notify call!');
    docs.keys.forEach((String id) {
      Document doc = docs[id];
      _log('Printing document with id: ${doc.docid}');

      doc.properties.keys.forEach((String key) {
        _log('$key: ${doc.properties[key]}');
      });
    });

    _homeKey.currentState?.setState(() {
      _homeKey.currentState._exampleDoc = docs[_kDocId];
    });
  }
}

class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  final StoryProxy _story = new StoryProxy();
  final LinkProxy _link = new LinkProxy();

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
    _log('ModuleImpl::initialize call');

    // Bind the provided handles to our proxy objects.
    _story.ctrl.bind(storyHandle);
    _link.ctrl.bind(linkHandle);

    // Register the link watcher.
    _link.watchAll(new LinkWatcherImpl().getHandle());

    _setValue(42);
  }

  @override
  void stop(void callback()) {
    _log('ModuleImpl::stop call');

    // Do some clean up here.

    // Invoke the callback to signal that the clean-up process is done.
    callback();
  }

  void _setValue(int newValue) {
    _link.addDocuments(<String, Document>{
      _kDocId: new Document.init(_kDocId, <String, Value>{
        _kCounterValueKey: new Value()..intValue = newValue,
      }),
    });
  }
}

class _HomeScreen extends StatefulWidget {
  _HomeScreen({Key key}) : super(key: key);

  @override
  _HomeScreenState createState() => new _HomeScreenState();
}

class _HomeScreenState extends State<_HomeScreen> {
  Document _exampleDoc;

  int get _currentValue {
    if (_exampleDoc == null ||
        _exampleDoc.properties == null ||
        !_exampleDoc.properties.containsKey(_kCounterValueKey) ||
        _exampleDoc.properties[_kCounterValueKey].tag != ValueTag.intValue) {
      return -1;
    }

    return _exampleDoc.properties[_kCounterValueKey].intValue;
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
  _log('Child module started with context: $_context');

  _context.outgoingServices.addServiceForName(
    (InterfaceRequest<Module> request) {
      _log('Received binding request for Module');
      _module = new ModuleImpl();
      _module.bind(request);
    },
    Module.serviceName,
  );

  runApp(new MaterialApp(
    title: 'Counter Child',
    home: new _HomeScreen(key: _homeKey),
    theme: new ThemeData(primarySwatch: Colors.blue),
  ));
}
