// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.ledger.services.public/ledger.fidl.dart';
import 'package:apps.modular.services.component/component_context.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'generator.dart';

typedef void ItemsChangedCallback(Map);

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

/// An implementation of the [Module] interface.
class TodoModule extends Module implements PageWatcher {
  final ModuleBinding _binding = new ModuleBinding();

  final PageWatcherBinding _page_watcher_binding = new PageWatcherBinding();

  final LedgerProxy _ledger = new LedgerProxy();

  final PageProxy _page = new PageProxy();

  final StoryProxy _story = new StoryProxy();

  final ComponentContextProxy _componentContext = new ComponentContextProxy();

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

    _story.getComponentContext(_componentContext.ctrl.request());
    _componentContext.getLedger(
        _ledger.ctrl.request(), _handleResponse("getLedger"));
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
    _ledger.ctrl.close();
    _page.ctrl.close();
    _story.ctrl.close();

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
