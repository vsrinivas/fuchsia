// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ledger/src/ledger_impl.dart';
import 'package:modular_services/ledger/ledger.mojom.dart';
import 'package:modular_services/synced_store/synced_store.mojom.dart';
import 'package:mojo/application.dart';
import 'package:mojo/core.dart';

class LedgerApplication extends Application {
  SyncedStoreProxy _syncedStoreProxy;
  LedgerImpl _ledger;

  LedgerApplication.fromHandle(MojoHandle handle) : super.fromHandle(handle);

  @override
  void initialize(List<String> args, String url) {
    _syncedStoreProxy = _initializeMockSyncStore(args, url);
    _ledger = new LedgerImpl(_syncedStoreProxy);
  }

  SyncedStoreProxy _initializeMockSyncStore(List<String> args, String url) {
    final SyncedStoreProxy proxy = new SyncedStoreProxy.unbound();
    connectToService('https://tq.mojoapps.io/mock_synced_store.mojo', proxy);
    return proxy;
  }

  @override
  void acceptConnection(String requestorUrl, String resolvedUrl,
      ApplicationConnection connection) {
    connection.provideService(Ledger.serviceName,
        (MojoMessagePipeEndpoint endpoint) {
      new LedgerStub.fromEndpoint(endpoint, _ledger);
    });
  }

  @override
  Future<List<dynamic>> close({bool immediate: false}) async {
    final List<Future<dynamic>> toWait = <Future<dynamic>>[
      _ledger?.close(immediate: immediate) ?? new Future<Null>.value(),
      _syncedStoreProxy.close(immediate: immediate),
      super.close(immediate: immediate)
    ];
    return Future.wait(toWait);
  }
}
