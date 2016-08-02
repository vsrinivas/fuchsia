// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core';

import 'package:modular_services/synced_store/synced_store.mojom.dart';
import 'package:mojo/application.dart';
import 'package:mojo_apptest/apptest.dart';

class TestHelper {
  final Application _application;
  final Uri _syncedStoreTestUri;

  final SyncedStoreProxy _syncedStoreProxy = new SyncedStoreProxy.unbound();
  final Map<String, String> _storedData = <String, String>{};

  TestHelper(this._application, String uri)
      : _syncedStoreTestUri = Uri.parse(uri) {
    String uri = "/syncbase_synced_store.mojo";
    String syncedStoreServiceUri = _syncedStoreTestUri.resolve(uri).toString();
    _application.connectToService(syncedStoreServiceUri, _syncedStoreProxy);
  }

  SyncedStore get syncedStore => _syncedStoreProxy;

  Future<Null> checkPut(final Map<String, String> keyValues) async {
    final String errorReason = "For keyValues=${keyValues.toString()}.";
    _storedData.addAll(keyValues);
    final Completer<SyncedStoreStatus> completer =
        new Completer<SyncedStoreStatus>();
    syncedStore.put(keyValues,
        (final SyncedStoreStatus status) => completer.complete(status));
    expect(await completer.future, SyncedStoreStatus.ok, reason: errorReason);
  }

  Future<Null> checkGet(final List<String> keys) async {
    final String errorReason = "For keys=${keys.toString()}.";
    final Completer<Map<String, String>> completer =
        new Completer<Map<String, String>>();
    syncedStore.get(keys,
        (final Map<String, String> keyValues, final SyncedStoreStatus status) {
      expect(status, SyncedStoreStatus.ok, reason: errorReason);
      completer.complete(keyValues);
    });
    final Map<String, String> keyValues = await completer.future;
    int expectedLength = 0;
    // All keys must be in the result (with matching values).
    for (String key in keys) {
      if (_storedData.containsKey(key)) {
        expect(keyValues[key], _storedData[key], reason: errorReason);
        expectedLength++;
      } else {
        expect(keyValues.containsKey(key), false, reason: errorReason);
      }
    }
    // The result contains more results than it should.
    expect(keyValues.length, expectedLength, reason: errorReason);
  }

  Future<Null> checkGetByPrefix(
      final List<String> keyPrefixes, final List<String> expectedKeys) async {
    final String errorReason = "For keyPrefixes=${keyPrefixes.toString()}.";
    final Completer<Map<String, String>> completer =
        new Completer<Map<String, String>>();
    syncedStore.getByPrefix(keyPrefixes,
        (final Map<String, String> keyValues, final SyncedStoreStatus status) {
      expect(status, SyncedStoreStatus.ok, reason: errorReason);
      completer.complete(keyValues);
    });
    final Map<String, String> keyValues = await completer.future;

    for (String key in expectedKeys) {
      expect(_storedData[key], keyValues[key], reason: errorReason);
    }
    expect(keyValues.length, expectedKeys.length, reason: errorReason);
  }

  Future<Null> checkGetByValueAttributes(final String keyPrefix,
      final String expectedFieldValues, final List<String> expectedKeys) async {
    final String errorReason =
        "For keyPrefix=$keyPrefix, expectedFieldValues=$expectedFieldValues.";

    final Completer<Map<String, String>> completer =
        new Completer<Map<String, String>>();

    syncedStore.getByValueAttributes(keyPrefix, expectedFieldValues,
        (final Map<String, String> keyValues, final SyncedStoreStatus status) {
      expect(status, SyncedStoreStatus.ok, reason: errorReason);
      completer.complete(keyValues);
    });
    final Map<String, String> keyValues = await completer.future;

    for (String key in expectedKeys) {
      expect(keyValues[key] != null, true, reason: errorReason);
      expect(keyValues[key], _storedData[key], reason: errorReason);
    }
    expect(keyValues.length, expectedKeys.length, reason: errorReason);
  }

  Future<Null> close() async {
    await _syncedStoreProxy.close();
  }
}

class _TestObserver implements SyncedStoreObserver {
  final SyncedStoreObserverStub stub;

  int _rowCount = 0;
  int _onChangeCount = 0;

  Completer<Null> _completer;

  _TestObserver() : stub = new SyncedStoreObserverStub.unbound() {
    stub.impl = this;
  }

  int get rowCount => _rowCount;

  int get onChangeCount => _onChangeCount;

  Future<Null> waitForChange() {
    _completer = new Completer<Null>();
    return _completer.future.timeout(new Duration(seconds: 2),
        onTimeout: () => fail("Failed to receive change from SyncedStore."));
  }

  @override
  void onChange(Map<String, String> changes, void callback()) {
    assert(_completer != null);
    _onChangeCount++;
    _rowCount += changes.length;
    if (!_completer.isCompleted) {
      _completer.complete();
    }
    _completer = null;
    callback();
  }
}

void syncedStoreTests(Application app, String url) {
  TestHelper helper;

  setUp(() async {
    helper = new TestHelper(app, url);
  });

  tearDown(() async {
    await helper.close();
  });

  // TODO(nellyv): test authentication.

  test('Test put/get', () async {
    String key1 = "key1";
    String value1 = "value1";
    String value1v2 = "value1_2";
    String key2 = "key2";
    Map<String, String> keyValues = {
      "key3": "value3",
      "key4": "value4",
      "key5": "value5",
    };

    // Put an get a single key-value pair.
    await helper.checkPut({key1: value1});
    await helper.checkGet([key1]);

    // Empty value should be returned when searching for a non-existing key.
    await helper.checkGet([key2]);

    // Replace a value of an existing key.
    await helper.checkPut({key1: value1v2});
    await helper.checkGet([key1]);

    // Put multiple keys and find them.
    await helper.checkPut(keyValues);
    await helper.checkGet(keyValues.keys.toList());
  });

  test('Test getByPrefix', () async {
    final String testPrefix = "testByPrefix_";
    final List<String> testKeys = [
      testPrefix + "pre/key_1",
      testPrefix + "pref/key_2",
      testPrefix + "prefi/key_3",
      testPrefix + "prefix/key_4",
    ];
    Map<String, String> keyValues = {
      testKeys[0]: "value1",
      testKeys[1]: "value2",
      testKeys[2]: "value3",
      testKeys[3]: "value4",
    };

    await helper.checkPut(keyValues);

    // Exact key matching.
    await helper.checkGetByPrefix([testKeys[1]], [testKeys[1]]);

    // Prefix of multiple keys.
    await helper.checkGetByPrefix(
        [testPrefix + "pref"], [testKeys[1], testKeys[2], testKeys[3]]);

    // Prefix doesn't exist.
    await helper.checkGetByPrefix(["aaaa"], []);

    // Multiple prefixes.
    await helper.checkGetByPrefix(
        [testPrefix + "pre", testPrefix + "prefi"], keyValues.keys.toList());
  });

  test('Test getByValueAttributes', () async {
    final List<String> testPrefixes = [
      "testByAtt_prefix_1/",
      "testByAtt_prefix_2/",
    ];
    final List<String> testKeys = [
      testPrefixes[0] + "key1",
      testPrefixes[0] + "key2",
      testPrefixes[1] + "key3",
      testPrefixes[1] + "key4",
    ];
    final Map<String, String> keyValues = {
      // Empty json.
      testKeys[0]: "{}",
      // int, String, bool and map values.
      testKeys[1]:
          '{"a":1,"b":{"ba":"21","bb":"22"},"c":{"ca":true,"cb":false}}',
      // List of values.
      testKeys[2]: '{"a":["123","456","789"]}',
      // List of maps.
      testKeys[3]: '{"a":[{"id":"123"},{"id":"456"},{"id":"789"}]}',
    };

    await helper.checkPut(keyValues);

    // Empty JSONs should match all values.
    await helper.checkGetByValueAttributes(
        testPrefixes[0], '{}', [testKeys[0], testKeys[1]]);

    // Check bool and int values.
    await helper.checkGetByValueAttributes(
        testPrefixes[0], '{"a":1,"c":{"cb":false}}', [testKeys[1]]);

    // Check map values.
    await helper.checkGetByValueAttributes(
        testPrefixes[0], '{"b":{"ba":"21"},"c":{"ca":true}}', [testKeys[1]]);

    // Εlements in lists should be a subset of those in the stored row.
    await helper
        .checkGetByValueAttributes(testPrefixes[1], '{"a":["456", "000"]}', []);
    await helper.checkGetByValueAttributes(
        testPrefixes[1], '{"a":["456","789"]}', [testKeys[2]]);
    await helper.checkGetByValueAttributes(
        testPrefixes[1], '{"a":[{"id":"456"},{"id":"789"}]}', [testKeys[3]]);
  });

  test('Test observer', () async {
    String prefix = "observer";

    final _TestObserver observer = new _TestObserver();

    Map<String, String> keyValues = {
      "$prefix/key1": "value1",
      "dummy/key2": "value2",
    };
    await helper.checkPut(keyValues);

    // Add the observer and check that previous changes are not received.
    final Completer completer = new Completer();
    helper.syncedStore.addObserver(prefix, null, observer.stub,
        (final SyncedStoreStatus status) => completer.complete());
    await completer.future;
    expect(observer.onChangeCount, 0);
    expect(observer.rowCount, 0);

    // Add some new values and receive the ones with the expected prefix.
    keyValues = {
      "$prefix/key3": "value3",
      "$prefix/key4": "value4",
      "dummy/key5": "value5",
    };
    Future<Null> change = observer.waitForChange();
    await helper.checkPut(keyValues);
    await change;
    expect(observer.onChangeCount, 1);
    expect(observer.rowCount, 2);

    // Add some more and receive them.
    keyValues = {"dummy/key6": "value6", "$prefix/key7": "value7",};
    change = observer.waitForChange();
    await helper.checkPut(keyValues);
    await change;
    expect(observer.onChangeCount, 2);
    expect(observer.rowCount, 3);

    observer.stub.close();
  });
}

void main(List<String> args, Object handleToken) {
  runAppTests(handleToken, [syncedStoreTests]);
}
