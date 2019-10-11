// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:device_settings/model.dart';
import 'package:fidl_fuchsia_recovery/fidl_async.dart' as recovery;
import 'package:fidl_fuchsia_update/fidl_async.dart' as update;
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart'
    as channelcontrol;
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

class TestSystemInterface extends Mock implements SystemInterface {
  @override
  int get currentTime => 0;

  @override
  void dispose() {}
}

class MockUpdateManager extends Mock implements update.Manager {}

class MockChannelControl extends Mock implements channelcontrol.ChannelControl {
}

class MockFactoryReset extends Mock implements recovery.FactoryReset {}

void main() {
  // Ensure start only reacts to the first invocation.
  test('test_start', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.getCurrentChannel()).thenAnswer((_) => Future.value(''));

    when(sysInterface.getChannelList()).thenAnswer((_) => Future.value([]));

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    await model.start();

    // Ensure resolver is interacted with on the first start.
    verify(sysInterface.getCurrentChannel());
    verify(sysInterface.getChannelList());

    // We should not be waiting on anything in the second start as it should be
    // an early return.
    await model.start();

    // Ensure resolver has not been interacted with since the first start.
    verifyNever(sysInterface.getCurrentChannel());
    verifyNever(sysInterface.getChannelList());
  });

  // Ensure checkForSystemUpdate has the intended side effects.
  test('test_check_for_updates', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.getCurrentChannel()).thenAnswer((_) => Future.value(''));

    when(sysInterface.getChannelList()).thenAnswer((_) => Future.value([]));

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

    var currentChannelCompleter = Completer<String>();
    var channelListCompleter = Completer<List<String>>();

    when(sysInterface.getCurrentChannel())
        .thenAnswer((_) => currentChannelCompleter.future);

    when(sysInterface.getChannelList())
        .thenAnswer((_) => channelListCompleter.future);

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    final Future startFuture = model.start();

    // On start, the model should report it is updating as the proxy has not
    // returned
    expect(model.channelUpdating, true);
    currentChannelCompleter.complete('');
    channelListCompleter.complete([]);
    await startFuture;
    expect(model.channelUpdating, false);

    // Reset update completer so it can be used in the next step.
    currentChannelCompleter = Completer<String>();
    channelListCompleter = Completer<List<String>>();

    when(sysInterface.setTargetChannel(any)).thenAnswer((_) async {});

    // make sure we are also updating when selecting a channel.
    Future selectFuture = model.selectChannel('stable');
    expect(model.channelUpdating, true);
    currentChannelCompleter.complete('');
    channelListCompleter.complete([]);
    await selectFuture;

    // Make sure we are also asking to reboot when selecting a channel.
    expect(model.showRebootConfirmation, true);

    // Make sure that a reboot is needed to finish the changing of the channel.
    expect(model.needsRebootToFinish, true);

    // and have not attempted to reboot.
    verifyNever(sysInterface.reboot());
  });

  // Ensure the factory reset service gets called.
  test('test_check_for_factory_reset', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.getCurrentChannel()).thenAnswer((_) => Future.value(''));

    when(sysInterface.getChannelList()).thenAnswer((_) => Future.value([]));

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

    when(sysInterface.getCurrentChannel()).thenAnswer((_) => Future.value(''));

    when(sysInterface.getChannelList()).thenAnswer((_) => Future.value([]));

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

  test('test_ask_for_reboot_after_channel_change', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.getCurrentChannel()).thenAnswer((_) => Future.value(''));

    when(sysInterface.getChannelList()).thenAnswer((_) => Future.value([]));

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    await model.start();

    // Select a new channel.
    await model.selectChannel('stable');

    // When the channel is changed, it should show a reboot confirmation.
    expect(model.showRebootConfirmation, true);
    verifyNever(sysInterface.reboot());

    // Requesting a reboot should do so.
    model.reboot();
    verify(sysInterface.reboot());
  });

  test('test_reboot_dismissal_does_not_reboot', () async {
    final TestSystemInterface sysInterface = TestSystemInterface();

    when(sysInterface.getCurrentChannel()).thenAnswer((_) => Future.value(''));

    when(sysInterface.getChannelList()).thenAnswer((_) => Future.value([]));

    final DeviceSettingsModel model = DeviceSettingsModel(sysInterface);
    await model.start();

    // Select a new channel.
    await model.selectChannel('stable');

    // When the channel is changed, it should show a reboot confirmation.
    expect(model.showRebootConfirmation, true);
    verifyNever(sysInterface.reboot());

    // Dismissing the reboot should now cause a reboot.
    await model.cancelReboot();
    expect(model.showRebootConfirmation, false);
    verifyNever(sysInterface.reboot());

    // But a reboot should still be needed.
    expect(model.needsRebootToFinish, true);

    // And another request to reboot should bring up the confirmation dialog.
    model.reboot();
    expect(model.showRebootConfirmation, true);

    // But not have triggered a reboot.
    verifyNever(sysInterface.reboot());

    // And a call to reboot should then trigger the reboot.
    model.reboot();
    verify(sysInterface.reboot());
  });
}
