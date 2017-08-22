// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.ledger.services.public/ledger.fidl.dart';
import 'package:apps.modular.services.component/component_context.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:lib.fidl.dart/core.dart';

import 'generator.dart';

typedef void ItemsChangedCallback(Map<List<int>, String> map);

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

String _vmoToString(Vmo vmo) {
  var sizeResult = vmo.getSize();
  if (sizeResult.status != NO_ERROR) {
    _log("Unable to retrieve vmo size: ${sizeResult.status}");
    return "";
  }

  var readResult = vmo.read(sizeResult.size);
  if (readResult.status != NO_ERROR) {
    _log("Unable to read from vmo: ${readResult.status}");
    return "";
  }
  if (readResult.numBytes != sizeResult.size) {
    _log("Unexpected count of bytes read.");
    return "";
  }
  return readResult.bytesAsUTF8String();
}

/// An implementation of the [Module] interface.
class TodoModule extends Module implements PageWatcher {
  final ModuleBinding _binding = new ModuleBinding();

  final PageWatcherBinding _pageWatcherBinding = new PageWatcherBinding();

  final LedgerProxy _ledger = new LedgerProxy();

  final PageProxy _page = new PageProxy();

  final ModuleContextProxy _moduleContext = new ModuleContextProxy();

  final ComponentContextProxy _componentContext = new ComponentContextProxy();

  /// Watchers to be notified of changes in the list of items.
  final List<ItemsChangedCallback> _callbacks = <ItemsChangedCallback>[];

  /// Generator of the todo items.
  final Generator _generator = new Generator();

  /// Implementation of Module.initialize().
  @override
  void initialize(
      InterfaceHandle<ModuleContext> moduleContextHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _log('TodoModule::initialize()');

    _moduleContext.ctrl.bind(moduleContextHandle);

    _moduleContext.getComponentContext(_componentContext.ctrl.request());
    _componentContext.getLedger(
        _ledger.ctrl.request(), _handleResponse("getLedger"));
    _ledger.getRootPage(_page.ctrl.request(), _handleResponse("getRootPage"));

    PageSnapshotProxy snapshot = new PageSnapshotProxy();
    _page.getSnapshot(snapshot.ctrl.request(), null,
        _pageWatcherBinding.wrap(this), _handleResponse("Watch"));

    _readItems(snapshot);
  }

  /// Implementation of Module.stop().
  @override
  void stop(void callback()) {
    _log('TodoModule.stop()');
    _ledger.ctrl.close();
    _page.ctrl.close();
    _moduleContext.ctrl.close();

    // Invoke the callback to signal that the clean-up process is done.
    callback();

    _binding.close();
  }

  /// Implementation of PageWatcher.onChange().
  @override
  // ignore: type_annotate_public_apis
  void onChange(PageChange pageChange, ResultState resultState, callback) {
    if (resultState != ResultState.completed &&
        resultState != ResultState.partialStarted) {
      callback(null);
      return;
    }
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

  /// Adds an item.
  void addItem() {
    _page.put(_generator.makeKey(), UTF8.encode(_generator.makeContent()),
        _handleResponse("Put"));
  }

  /// Removes an item.
  void removeItem(List<int> key) {
    _page.delete(key, _handleResponse("Delete"));
  }

  void _readItems(PageSnapshotProxy snapshot) {
    _getEntries(snapshot, (Status status, Map<List<int>, String> items) {
      if (status != Status.ok) {
        _log("getEntries: $status");
        return;
      }

      _callbacks.forEach((callback) => callback(items));
      snapshot.ctrl.close();
    });
  }

  void _getEntries(PageSnapshotProxy snapshot,
      void callback(Status status, Map<List<int>, String> items)) {
    _getEntriesRecursive(snapshot, <List<int>, String>{}, null, callback);
  }

  void _getEntriesRecursive(
      PageSnapshotProxy snapshot,
      Map<List<int>, String> items,
      List<int> token,
      void callback(Status status, Map<List<int>, String> items)) {
    snapshot.getEntries(null, token,
        (Status status, List<Entry> entries, List<int> nextToken) {
      if (status != Status.ok && status != Status.partialResult) {
        callback(status, {});
        return;
      }
      if (entries != null) {
        for (final entry in entries) {
          items[entry.key] = _vmoToString(entry.value);
        }
      }
      if (status == Status.ok) {
        callback(Status.ok, items);
        return;
      }
      _getEntriesRecursive(snapshot, items, nextToken, callback);
    });
  }
}
