// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:apps.modular.services.document_store/document.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/module_controller.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:apps.mozart.lib.flutter/child_view.dart';
import 'package:apps.mozart.services.views/view_token.fidl.dart';
import 'package:flutter/material.dart';
import 'package:lib.fidl.dart/bindings.dart';

final ApplicationContext _context = new ApplicationContext.fromStartupInfo();

final String _kDocId = 'example-doc-id';
final String _kCounterValueKey = 'example-counter-key';

void _log(String msg) {
  print('[Example Parent] $msg');
}

class _HomeScreen extends StatefulWidget {
  _HomeScreen({Key key}) : super(key: key);

  @override
  _HomeScreenState createState() => new _HomeScreenState();
}

class _HomeScreenState extends State<_HomeScreen>
    implements Module, LinkWatcher {
  final ModuleBinding _moduleBinding = new ModuleBinding();
  final LinkWatcherBinding _linkWatcherBinding = new LinkWatcherBinding();

  final StoryProxy _story = new StoryProxy();
  final LinkProxy _link = new LinkProxy();

  ChildViewConnection _connFolderList;
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
  void initState() {
    super.initState();

    _log('_HomeScreenState::initState call. Registering Module service.');
    _context.outgoingServices.addServiceForName(
      (InterfaceRequest<dynamic> request) {
        _log('Received binding request for Module');
        _moduleBinding.bind(this, request);
      },
      Module.serviceName,
    );
  }

  @override
  void initialize(
    InterfaceHandle<Story> storyHandle,
    InterfaceHandle<Link> linkHandle,
    InterfaceHandle<Link> incomingServices,
    InterfaceRequest<Link> outgoingServices) {
    _log('_HomeScreenState::initialize call');

    // Bind the provided handles to our proxy objects.
    _story.ctrl.bind(storyHandle);
    _link.ctrl.bind(linkHandle);

    // Register the link watcher.
    _link.watchAll(_linkWatcherBinding.wrap(this));

    // Duplicate the link handle to pass it down to the sub-module.
    LinkProxy linkForFolderList = new LinkProxy();
    _link.dup(linkForFolderList.ctrl.request());

    ModuleControllerProxy moduleController = new ModuleControllerProxy();
    ViewOwnerProxy viewOwner = new ViewOwnerProxy();

    // Start the folder list module.
    _story.startModule(
      'file:///system/apps/example_flutter_child_module',
      linkForFolderList.ctrl.unbind(),
      null, null,
      moduleController.ctrl.request(),
      viewOwner.ctrl.request(),
    );

    setState(() {
      _connFolderList = new ChildViewConnection(viewOwner.ctrl.unbind());
    });
  }

  @override
  void stop(void callback()) {
    _log('_HomeScreenState::stop call');

    // Do some clean up here.

    // Invoke the callback to signal that the clean-up process is done.
    callback();
  }

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

    setState(() {
      _exampleDoc = docs[_kDocId];
    });
  }

  @override
  Widget build(BuildContext context) {
    List<Widget> children = <Widget>[
      new Text('I am the parent module!'),
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

    if (_connFolderList != null) {
      children.add(new Flexible(
        flex: 1,
        child: new ChildView(connection: _connFolderList),
      ));
    }

    return new Material(
      color: Colors.orange[200],
      child: new Container(
        child: new Column(children: children),
      ),
    );
  }

  void _handleIncrease() {
    _setValue(_currentValue + 1);
  }

  void _handleDecrease() {
    _setValue(_currentValue - 1);
  }

  void _setValue(int newValue) {
    _link.addDocuments(<String, Document>{
      _kDocId: new Document.init(_kDocId, <String, Value>{
        _kCounterValueKey: new Value()..intValue = newValue,
      }),
    });
  }
}

/// Main entry point to the example parent module.
void main() {
  _log('Parent module started with context: $_context');

  runApp(new MaterialApp(
    title: 'Example Parent',
    home: new _HomeScreen(),
    theme: new ThemeData(primarySwatch: Colors.orange),
  ));
}
