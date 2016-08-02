// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_services/synced_store/synced_store.mojom.dart';
import 'package:mojo/application.dart';
import 'package:mojo/core.dart';

import 'src/mock_synced_store.dart';

class SyncedStoreApplication extends Application {
  SyncedStore _syncedStore;

  SyncedStoreApplication.fromHandle(MojoHandle handle)
      : super.fromHandle(handle);

  @override
  void initialize(List<String> args, String url) {
    _syncedStore = new MockSyncedStore();
  }

  @override
  void acceptConnection(String requestorUrl, String resolvedUrl,
      ApplicationConnection connection) {
    connection.provideService(SyncedStore.serviceName,
        (MojoMessagePipeEndpoint endpoint) {
      new SyncedStoreStub.fromEndpoint(endpoint, _syncedStore);
    });
  }

  @override
  Future<Null> close({bool immediate: false}) async {
    _syncedStore = null;
  }
}
