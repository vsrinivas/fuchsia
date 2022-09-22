// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.9

import 'package:fuchsia_component_test/realm_builder.dart';
import 'package:fidl_fuchsia_component/fidl_async.dart' as fcomponent;
import 'package:fidl_fuchsia_diagnostics/fidl_async.dart' as fdiagnostics;
import 'package:fuchsia_inspect/reader.dart';
import 'package:pedantic/pedantic.dart';
import 'package:test/test.dart';

const serverName = 'dart-inspect-wrapper-test';

const testComponentName = 'inspect_test_component';
const testComponentUrl = '#meta/$testComponentName.cm';
const testComponentBinder = "fuchsia.component.TestComponentBinder";

class TestTopology {
  RealmInstance instance;

  TestTopology._create(RealmInstance instance) {
    this.instance = instance;
  }

  void startTestComponent({int id = 0}) {
    final proxy = fcomponent.BinderProxy();
    this.instance.root.connectToNamedProtocolAtExposedDir(
        '${testComponentBinder}${id}', proxy.ctrl.request().passChannel());
  }

  String testComponentMoniker({int id = 0}) {
    return 'realm_builder\\:${this.instance.root.childName}/${testComponentName}-${id}';
  }

  static Future<TestTopology> create({int testComponents = 1}) async {
    final builder = await RealmBuilder.create();
    for (int i = 0; i < testComponents; i++) {
      final testComponent = await builder.addChild(
          '${testComponentName}-${i}', testComponentUrl, ChildOptions());
      await builder.addRoute(Route()
        ..capability(ProtocolCapability("fuchsia.logger.LogSink"))
        ..from(Ref.parent())
        ..to(Ref.child(testComponent)));
      await builder.addRoute(
        Route()
          ..capability(ProtocolCapability(
            fcomponent.Binder.$serviceName,
            as: '${testComponentBinder}${i}',
          ))
          ..from(Ref.child(testComponent))
          ..to(Ref.parent()),
      );
    }
    return TestTopology._create(await builder.build());
  }

  void dispose() {
    this.instance.root.close();
  }
}

void main() async {
  test('no_selector', () async {
    final testTopology = await TestTopology.create();
    testTopology.startTestComponent();

    final reader = ArchiveReader.forInspect();

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) =>
            snapshot.length >= 1 &&
            _monikerList(snapshot)
                .contains(testTopology.testComponentMoniker()));

    expect(snapshot.length, greaterThanOrEqualTo(1));
    expect(
        _monikerList(snapshot), contains(testTopology.testComponentMoniker()));
    testTopology.dispose();
  });

  test('component_selector', () async {
    final testTopology = await TestTopology.create();
    testTopology.startTestComponent();

    final reader = ArchiveReader.forInspect(selectors: [
      Selector.fromRawSelector('${testTopology.testComponentMoniker()}:root')
    ]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].moniker, testTopology.testComponentMoniker());
    expect(snapshot[0].payload['root']['int'], 3);
    expect(snapshot[0].payload['root']['lazy-node']['a'], 'test');

    testTopology.dispose();
  });

  test('hierarchy_selector', () async {
    final testTopology = await TestTopology.create();
    testTopology.startTestComponent();

    final reader = ArchiveReader.forInspect(selectors: [
      Selector.fromRawSelector(
          '${testTopology.testComponentMoniker()}:root/lazy-node')
    ]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot is List && snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].moniker, testTopology.testComponentMoniker());
    expect(snapshot[0].payload['root']['int'], isNull);
    expect(snapshot[0].payload['root']['lazy-node']['a'], 'test');

    testTopology.dispose();
  });

  test('property_selector', () async {
    final testTopology = await TestTopology.create();
    testTopology.startTestComponent();

    final reader = ArchiveReader.forInspect(selectors: [
      Selector.fromRawSelector(
          '${testTopology.testComponentMoniker()}:root:int')
    ]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot is List && snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].moniker, testTopology.testComponentMoniker());
    expect(snapshot[0].payload['root']['int'], 3);
    expect(snapshot[0].payload['root']['lazy-node'], isNull);

    testTopology.dispose();
  });

  test('multiple_selectors', () async {
    final testTopology = await TestTopology.create();
    testTopology.startTestComponent();

    final reader = ArchiveReader.forInspect(selectors: [
      Selector.fromRawSelector(
          '${testTopology.testComponentMoniker()}:root:int'),
      Selector.fromRawSelector(
          '${testTopology.testComponentMoniker()}:root/lazy-node')
    ]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot is List && snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].moniker, testTopology.testComponentMoniker());
    expect(snapshot[0].payload['root']['int'], 3);
    expect(snapshot[0].payload['root']['lazy-node']['a'], 'test');

    testTopology.dispose();
  });

  test('multiple_batches', () async {
    var testComponentMonikers = <String>{};
    final testTopology = await TestTopology.create(
        testComponents: fdiagnostics.maximumEntriesPerBatch + 1);
    for (var i = 0; i < fdiagnostics.maximumEntriesPerBatch + 1; i++) {
      testTopology.startTestComponent(id: i);
      testComponentMonikers.add(testTopology.testComponentMoniker(id: i));
    }

    final reader = ArchiveReader.forInspect();

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) =>
            snapshot is List &&
            snapshot.length >= 2 &&
            Set.from(_monikerList(snapshot))
                .containsAll(testComponentMonikers));

    expect(snapshot.length, greaterThanOrEqualTo(2));
    expect(_monikerList(snapshot), containsAll(testComponentMonikers));

    testTopology.dispose();
  }, timeout: Timeout(Duration(seconds: 90)));
}

/// Returns a [List] of all component monikers in the given inspect [json].
List<String> _monikerList(List<DiagnosticsData> diagnosticsDataList) =>
    diagnosticsDataList.map((e) => e.moniker).toList();
