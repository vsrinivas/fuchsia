// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.ledger.services.public/ledger.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

void _log(String msg) {
  print('[Todo Ledger Example] $msg');
}

dynamic _handleResponse(String description) {
  return (Status status) {
    if (status != Status.ok) {
      _log("$description: $status");
    }
  };
}

typedef void ItemsChangedCallback(Map);

class Generator {
  static const List<String> _actions = const [
    "acquire",
    "cancel",
    "consider",
    "draw",
    "evaluate",
    "celebrate",
    "find",
    "identify",
    "meet with",
    "plan",
    "solve",
    "study",
    "talk to",
    "think about",
    "write an article about",
    "check out",
    "order",
    "write a spec for",
    "order",
    "track down",
    "memorize",
    "git checkout",
  ];

  static const List<String> _objects = const [
    "Christopher Columbus",
    "PHP",
    "a better way forward",
    "a glass of wine",
    "a good book on C++",
    "a huge simulation we are all part of",
    "a nice dinner out",
    "a sheep",
    "an AZERTY keyboard",
    "hipster bars south of Pigalle",
    "kittens",
    "manganese",
    "more cheese",
    "some bugs",
    "staticly-typed programming languages",
    "the cryptographic primitives",
    "the espresso machine",
    "the law of gravity",
    "the neighbor",
    "the pyramids",
    "the society",
    "velocity",
  ];

  final Random _random = new Random(new DateTime.now().millisecondsSinceEpoch);

  String makeContent() {
    String action = _actions[_random.nextInt(_actions.length)];
    String object = _objects[_random.nextInt(_objects.length)];
    return "$action $object";
  }

  List<int> makeKey() {
    List<int> key = <int>[];
    for (int i = 0; i < 16; i++) {
      key.add(_random.nextInt(256));
    }
    return key;
  }
}

/// An implementation of the [Module] interface.
///
/// Note that this class is currently conflating multiple responsibilities that
/// we might want to separate: module bookkeeping and boilerplate, ledger
/// access, notifications and data model all live here.
class TodoModule extends Module implements PageWatcher {
  final ModuleBinding _binding = new ModuleBinding();

  final PageWatcherBinding _page_watcher_binding = new PageWatcherBinding();

  final LedgerProxy _ledger = new LedgerProxy();

  final PageProxy _page = new PageProxy();

  final StoryProxy _story = new StoryProxy();

  /// Watchers to be notified of changes in the list of items.
  final List<ItemsChangedCallback> _callbacks = <ItemsChangedCallback>[];

  /// Generator of the todo items.
  final Generator _generator = new Generator();

  /// Implementation of Module.initialize().
  @override
  void initialize(
      InterfaceHandle<Story> storyHandle,
      InterfaceHandle<Link> linkHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _log('TodoModule::initialize()');

    _story.ctrl.bind(storyHandle);

    _story.getLedger(_ledger.ctrl.request(), _handleResponse("getLedger"));
    _ledger.getRootPage(_page.ctrl.request(), _handleResponse("getRootPage"));

    PageSnapshotProxy snapshot = new PageSnapshotProxy();
    _page.getSnapshot(snapshot.ctrl.request(), _page_watcher_binding.wrap(this),
        _handleResponse("Watch"));

    _readItems(snapshot);
    _changeItems();
  }

  /// Implementation of Module.stop().
  @override
  void stop(void callback()) {
    _log('TodoModule::stop()');
    _ledger.ctrl.stop();
    _page.ctrl.stop();
    _story.ctrl.stop();

    // Invoke the callback to signal that the clean-up process is done.
    callback();
  }

  /// Implementation of PageWatcher.onChange().
  @override
  void onChange(PageChange pageChange, callback) {
    PageSnapshotProxy snapshot = new PageSnapshotProxy();
    callback(snapshot.ctrl.request());
    _readItems(snapshot);
  }

  /// Binds an [InterfaceRequest] for a [Module] interface to this object.
  void bind(InterfaceRequest<Module> request) {
    _binding.bind(this, request);
  }

  /// Adds a watcher to be notified when the todo items change.
  void watch(ItemsChangedCallback callback) => _callbacks.add(callback);

  void _readItems(PageSnapshotProxy snapshot) {
    snapshot.getEntries(null, null,
        (Status status, List<Entry> entries, List<int> token) {
      if (status != Status.ok) {
        _log("getEntries: $status");
      }

      Map<List<int>, String> items = <List<int>, String>{};
      if (entries != null) {
        for (var entry in entries) {
          items[entry.key] = UTF8.decode(entry.value.bytes);
        }
      }

      _callbacks.forEach((callback) => callback(items));
      snapshot.ctrl.close();
    });
  }

  Future<Null> _changeItems() async {
    while (true) {
      await new Future.delayed(const Duration(seconds: 3));
      _page.put(_generator.makeKey(), UTF8.encode(_generator.makeContent()),
          _handleResponse("Put"));
    }
  }
}

class TodoList extends StatefulWidget {
  TodoList(TodoModule this._module);

  final TodoModule _module;

  @override
  TodoListState createState() => new TodoListState(_module);
}

class TodoListState extends State<TodoList> {
  TodoListState(TodoModule this._module) {
    _module.watch((Map<List<int>, String> items) => setState(() {
          _items = items;
        }));
  }

  final TodoModule _module;

  Map<List<int>, String> _items = <List<int>, String>{};

  @override
  Widget build(BuildContext context) {
    String res = "";
    for (String item in _items.values) {
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
    title: 'Todo (Ledger)',
    home: new TodoList(module),
    theme: new ThemeData(primarySwatch: Colors.blue),
  ));
}
