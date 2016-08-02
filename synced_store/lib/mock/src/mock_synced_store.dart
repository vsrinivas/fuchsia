// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_services/synced_store/synced_store.mojom.dart';

/// A mock implementation of the SyncedStore.
class MockSyncedStore implements SyncedStore {
  final Map<String, String> _map = <String, String>{};
  final Map<String, List<SyncedStoreObserverProxy>> _observers =
      <String, List<SyncedStoreObserverProxy>>{};

  @override
  void authenticate(String username,
          void callback(AuthData authData, SyncedStoreStatus status)) =>
      callback(
          new AuthData()
            ..displayName = username ?? "test"
            ..uid = ""
            ..avatar = "",
          SyncedStoreStatus.ok);

  @override
  void put(
      Map<String, String> keyValues, void callback(SyncedStoreStatus status)) {
    final Map<SyncedStoreObserver, Map<String, String>> toNotify =
        <SyncedStoreObserver, Map<String, String>>{};
    keyValues.forEach((String key, String value) {
      _map[key] = value;
      // Prepare the key-value pairs each of the observers should receive.
      _observers
          .forEach((String prefix, List<SyncedStoreObserverProxy> observers) {
        if (key.startsWith(prefix)) {
          for (SyncedStoreObserverProxy observer in observers) {
            final Map<String, String> map =
                toNotify.putIfAbsent(observer, () => {});
            map[key] = value;
          }
        }
      });
    });
    // Notify each observer at most once for this batch.
    toNotify.forEach((SyncedStoreObserver observer, Map<String, String> map) {
      observer.onChange(map, () {});
    });
    callback(SyncedStoreStatus.ok);
  }

  @override
  void get(List<String> keys,
      void callback(Map<String, String> keyValues, SyncedStoreStatus status)) {
    Map<String, String> keyValues = <String, String>{};
    for (String key in keys) {
      if (_map.containsKey(key)) {
        keyValues[key] = _map[key];
      }
    }
    callback(keyValues, SyncedStoreStatus.ok);
  }

  Map<String, String> _getByPrefix(List<String> keyPrefixes) {
    final Map<String, String> keyValues = <String, String>{};
    for (String prefix in keyPrefixes) {
      for (String key in _map.keys) {
        if (key.startsWith(prefix)) {
          keyValues[key] = _map[key];
        }
      }
    }
    return keyValues;
  }

  @override
  void getByPrefix(
          List<String> keyPrefixes,
          void callback(
              Map<String, String> keyValues, SyncedStoreStatus status)) =>
      callback(_getByPrefix(keyPrefixes), SyncedStoreStatus.ok);

  @override
  void getByValueAttributes(
          String keyPrefix,
          String expectedFieldValues,
          void callback(
              Map<String, String> keyValues, SyncedStoreStatus status)) =>
      callback(_getByPrefix([keyPrefix]), SyncedStoreStatus.ok);

  @override
  void addObserver(
      String keyPrefix,
      String fieldValues,
      SyncedStoreObserverInterface observer,
      void callback(SyncedStoreStatus status)) {
    if (fieldValues != null) {
      throw new Exception("Not implemented, yet.");
    }
    _observers[keyPrefix] ??= [];
    _observers[keyPrefix].add(observer);
    callback(SyncedStoreStatus.ok);
  }
}
