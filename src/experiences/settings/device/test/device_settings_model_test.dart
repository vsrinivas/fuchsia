// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:device_settings/model.dart';
import 'package:fidl_fuchsia_pkg/fidl_async.dart' as pkg;
import 'package:fidl_fuchsia_pkg_rewrite/fidl_async.dart' as pkg_rewrite;
import 'package:fidl_fuchsia_recovery/fidl_async.dart' as recovery;
import 'package:fidl_fuchsia_update/fidl_async.dart' as update;
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

class TestSystemInterface extends Mock implements SystemInterface {
  @override
  int get currentTime => 0;

  @override
  void dispose() {}
}

class MockUpdateManager extends Mock implements update.Manager {}

class MockRepositoryManager extends Mock implements pkg.RepositoryManager {}

class MockRewriteManager extends Mock implements pkg_rewrite.Engine {}

class MockRepositoryIterator extends Mock implements pkg.RepositoryIterator {}

class MockFactoryReset extends Mock implements recovery.FactoryReset {}

void main() {
  // Ensure start only reacts to the first invocation.
  test('test_start', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.listRepositories())
        .thenAnswer((_) => Stream.fromIterable([]));

    when(sysInterface.listRules()).thenAnswer((_) => Stream.fromIterable([]));

    when(sysInterface.listStaticRules())
        .thenAnswer((_) => Stream.fromIterable([]));

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    await model.start();

    // Ensure resolver is interacted with on the first start.
    verify(sysInterface.listRepositories());
    verify(sysInterface.listRules());
    verify(sysInterface.listStaticRules());

    // We should not be waiting on anything in the second start as it should be
    // an early return.
    await model.start();

    // Ensure resolver has not been interacted with since the first start.
    verifyNever(sysInterface.listRepositories());
    verifyNever(sysInterface.listRules());
    verifyNever(sysInterface.listStaticRules());
  });

  // Ensure checkForSystemUpdate has the intended side effects.
  test('test_check_for_updates', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.listRepositories())
        .thenAnswer((_) => Stream.fromIterable([]));
    when(sysInterface.listRules()).thenAnswer((_) => Stream.fromIterable([]));
    when(sysInterface.listStaticRules())
        .thenAnswer((_) => Stream.fromIterable([]));

    when(sysInterface.checkForSystemUpdate())
        .thenAnswer((_) => Future.value(true));

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    await model.start();

    // Starting the model should not check for an update.
    verifyNever(sysInterface.checkForSystemUpdate());

    final startLastUpdate = model.lastUpdate;
    await model.checkForUpdates();

    // Requesting an update check should result in the API call and an update
    // to the lastUpdate time.
    verify(sysInterface.checkForSystemUpdate());
    expect(startLastUpdate, isNot(model.lastUpdate));
  });

  // Makes sure the updating state properly reflects current amber proxy
  // activity.
  test('test_channel_updating_state', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    var repoCompleter = Completer();
    var ruleCompleter = Completer();
    var staticRuleCompleter = Completer();

    when(sysInterface.listRepositories()).thenAnswer((_) async* {
      for (var repo in await repoCompleter.future) {
        yield repo;
      }
    });
    when(sysInterface.listRules()).thenAnswer((_) async* {
      for (var rule in await ruleCompleter.future) {
        yield rule;
      }
    });
    when(sysInterface.listStaticRules()).thenAnswer((_) async* {
      for (var rule in await staticRuleCompleter.future) {
        yield rule;
      }
    });

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    final Future startFuture = model.start();

    // On start, the model should report it is updating as the proxy has not
    // returned
    expect(model.channelUpdating, true);
    repoCompleter.complete([]);
    ruleCompleter.complete([]);
    staticRuleCompleter.complete([]);
    await startFuture;
    expect(model.channelUpdating, false);

    // Reset update completer so it can be used in the next step.
    repoCompleter = Completer();
    ruleCompleter = Completer();
    staticRuleCompleter = Completer();

    when(sysInterface.updateRules(any)).thenAnswer((_) async {
      return 0;
    });

    // make sure we are also updating when selecting a channel.
    Future selectFuture = model.selectChannel(
        pkg.RepositoryConfig(repoUrl: 'fuchsia-pkg://example.com'));
    expect(model.channelUpdating, true);
    repoCompleter.complete([]);
    ruleCompleter.complete([]);
    staticRuleCompleter.complete([]);
    await selectFuture;
    expect(model.channelUpdating, false);
  });

  // Ensure the factory reset service gets called.
  test('test_check_for_factory_reset', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.listRepositories())
        .thenAnswer((_) => Stream.fromIterable([]));

    when(sysInterface.listRules()).thenAnswer((_) => Stream.fromIterable([]));

    when(sysInterface.listStaticRules())
        .thenAnswer((_) => Stream.fromIterable([]));

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    await model.start();

    // Starting the model should not trigger factory reset.
    verifyNever(sysInterface.factoryReset());
    expect(model.showResetConfirmation, false);

    // When factory reset is called for the first time, it should show a confirmation.
    await model.factoryReset();
    expect(model.showResetConfirmation, true);
    verifyNever(sysInterface.factoryReset());

    // Requesting factory reset again should result in the API call.
    await model.factoryReset();
    verify(sysInterface.factoryReset());
  });

  // Ensure the factory reset service doesn't get called if it got cancelled.
  test('test_check_for_factory_reset', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.listRepositories())
        .thenAnswer((_) => Stream.fromIterable([]));

    when(sysInterface.listRules()).thenAnswer((_) => Stream.fromIterable([]));

    when(sysInterface.listStaticRules())
        .thenAnswer((_) => Stream.fromIterable([]));

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    await model.start();

    // Starting the model should not trigger factory reset.
    verifyNever(sysInterface.factoryReset());
    expect(model.showResetConfirmation, false);

    // When factory reset is called for the first time, it should show a confirmation.
    await model.factoryReset();
    expect(model.showResetConfirmation, true);
    verifyNever(sysInterface.factoryReset());

    // Cancelling factory reset should not result in a call.
    model.cancelFactoryReset();
    expect(model.showResetConfirmation, false);
    verifyNever(sysInterface.factoryReset());
  });
}
