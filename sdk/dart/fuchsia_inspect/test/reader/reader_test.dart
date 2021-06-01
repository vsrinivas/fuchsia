// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.9

import 'package:fidl_fuchsia_diagnostics/fidl_async.dart' as diagnostics;
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia_inspect/reader.dart';
import 'package:fuchsia_services/services.dart';
import 'package:pedantic/pedantic.dart';
import 'package:test/test.dart';

const serverName = 'dart-inspect-wrapper-test';

const testComponentName = 'inspect_test_component.cmx';
const testComponentUrl =
    'fuchsia-pkg://fuchsia.com/dart-archive-reader-test#meta/$testComponentName';

void main() async {
  test('no_selector', () async {
    const testComponentMoniker = testComponentName;
    final testComponentController = await _launchTestComponent();

    final reader = ArchiveReader.forInspect();

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) =>
            snapshot.length >= 2 &&
            _monikerList(snapshot).contains(testComponentMoniker));

    expect(snapshot.length, greaterThanOrEqualTo(2));
    expect(_monikerList(snapshot), contains(testComponentMoniker));

    await endTest(testComponentController);
  });

  test('component_selector', () async {
    const testComponentMoniker = testComponentName;
    final testComponentController = await _launchTestComponent();

    final reader = ArchiveReader.forInspect(
        selectors: [Selector.fromRawSelector('$testComponentMoniker:root')]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].metadata.componentUrl, testComponentUrl);
    expect(snapshot[0].moniker, testComponentMoniker);
    expect(snapshot[0].payload['root']['int'], 3);
    expect(snapshot[0].payload['root']['lazy-node']['a'], 'test');

    await endTest(testComponentController);
  });

  test('hierarchy_selector', () async {
    const testComponentMoniker = testComponentName;
    final testComponentController = await _launchTestComponent();

    final reader = ArchiveReader.forInspect(selectors: [
      Selector.fromRawSelector('$testComponentMoniker:root/lazy-node')
    ]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot is List && snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].metadata.componentUrl, testComponentUrl);
    expect(snapshot[0].moniker, testComponentMoniker);
    expect(snapshot[0].payload['root']['int'], isNull);
    expect(snapshot[0].payload['root']['lazy-node']['a'], 'test');

    await endTest(testComponentController);
  });

  test('property_selector', () async {
    const testComponentMoniker = 'inspect_test_component.cmx';
    final testComponentController = await _launchTestComponent();

    final reader = ArchiveReader.forInspect(selectors: [
      Selector.fromRawSelector('$testComponentMoniker:root:int')
    ]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot is List && snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].metadata.componentUrl, testComponentUrl);
    expect(snapshot[0].moniker, testComponentMoniker);
    expect(snapshot[0].payload['root']['int'], 3);
    expect(snapshot[0].payload['root']['lazy-node'], isNull);

    await endTest(testComponentController);
  });

  test('multiple_selectors', () async {
    const testComponentMoniker = testComponentName;
    final testComponentController = await _launchTestComponent();

    final reader = ArchiveReader.forInspect(selectors: [
      Selector.fromRawSelector('$testComponentMoniker:root:int'),
      Selector.fromRawSelector('$testComponentMoniker:root/lazy-node')
    ]);

    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) => snapshot is List && snapshot.isNotEmpty);

    expect(snapshot, hasLength(1));
    expect(snapshot[0].metadata.componentUrl, testComponentUrl);
    expect(snapshot[0].moniker, testComponentMoniker);
    expect(snapshot[0].payload['root']['int'], 3);
    expect(snapshot[0].payload['root']['lazy-node']['a'], 'test');

    await endTest(testComponentController);
  });

  test('multiple_batches', () async {
    final testComponentMonikers = <String>{};
    final testComponentControllers = <Future<ComponentController>>{};
    const environmentPrefix = 'multiple_batches_test_';
    for (var i = 0; i < diagnostics.maximumEntriesPerBatch + 1; i++) {
      final environmentName = '$environmentPrefix$i';
      testComponentMonikers.add('$environmentName/$testComponentName');
      final launchEnvironment = _createChildEnvironment(environmentName);
      unawaited(launchEnvironment.then((value) => testComponentControllers
          .add(_launchTestComponent(launchEnvironment: value))));
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

    for (final testComponentController in testComponentControllers) {
      await endTest(await testComponentController);
    }
  }, timeout: Timeout(Duration(seconds: 90)));
}

/// Creates a child environment with the given [name].
Future<EnvironmentProxy> _createChildEnvironment(String name) async {
  final environmentProxy = EnvironmentProxy();
  final incoming = Incoming.fromSvcPath()..connectToService(environmentProxy);

  final childEnvironment = EnvironmentProxy();
  final childEnvironmentController = EnvironmentControllerProxy();

  await environmentProxy.createNestedEnvironment(
    childEnvironment.ctrl.request(),
    childEnvironmentController.ctrl.request(),
    name,
    null,
    EnvironmentOptions(
      inheritParentServices: true,
      useParentRunners: true,
      deleteStorageOnDeath: true,
      killOnOom: true,
    ),
  );

  await incoming.close();

  return childEnvironment;
}

Future<ComponentControllerProxy> _launchTestComponent(
    {EnvironmentProxy launchEnvironment}) async {
  EnvironmentProxy environment;
  if (launchEnvironment != null) {
    environment = launchEnvironment;
  } else {
    final environmentProxy = EnvironmentProxy();
    final incoming = Incoming.fromSvcPath()..connectToService(environmentProxy);
    await incoming.close();
    environment = environmentProxy;
  }
  final launcher = LauncherProxy();
  await environment.getLauncher(launcher.ctrl.request());

  final launchInfo = LaunchInfo(url: testComponentUrl);

  final componentController = ComponentControllerProxy();

  await launcher.createComponent(
      launchInfo, componentController.ctrl.request());
  return componentController;
}

/// Returns a [List] of all component monikers in the given inspect [json].
List<String> _monikerList(List<DiagnosticsData> diagnosticsDataList) =>
    diagnosticsDataList.map((e) => e.moniker).toList();

Future<void> endTest(ComponentControllerProxy componentControllerProxy) async {
  await componentControllerProxy.kill();
  await for (var _ in componentControllerProxy.onTerminated) {}
}
