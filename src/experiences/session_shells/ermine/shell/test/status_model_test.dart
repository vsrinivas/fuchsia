// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_device_manager/fidl_async.dart';
import 'package:quickui/uistream.dart';
import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/status_model.dart';

void main() {
  StatusModel statusModel;
  final datetime = MockUiStream();
  final timezone = MockUiStream();
  final brightness = MockUiStream();
  final memory = MockUiStream();
  final battery = MockUiStream();
  final volume = MockUiStream();
  final bluetooth = MockUiStream();
  final channel = MockUiStream();
  final systemInformation = MockUiStream();
  final deviceManager = MockAdministratorProxy();
  var logoutCalled = false;

  setUp(() {
    statusModel = StatusModel(
      datetime: datetime,
      timezone: timezone,
      brightness: brightness,
      memory: memory,
      battery: battery,
      volume: volume,
      bluetooth: bluetooth,
      deviceManager: deviceManager,
      channel: channel,
      systemInformation: systemInformation,
      logout: () => logoutCalled = true,
    );
  });

  tearDown(() {
    final proxyController = MockProxyController();
    when(deviceManager.ctrl).thenReturn(proxyController);

    statusModel.dispose();
    verify(proxyController.close()).called(1);
    verify(datetime.dispose()).called(1);
    verify(timezone.dispose()).called(1);
    verify(brightness.dispose()).called(1);
    verify(memory.dispose()).called(1);
    verify(battery.dispose()).called(1);
    verify(volume.dispose()).called(1);
    verify(bluetooth.dispose()).called(1);
    verify(channel.dispose()).called(1);
    verify(systemInformation.dispose()).called(1);
  });

  test('Restart should call device reboot', () {
    statusModel.restartDevice();
    expect(verify(deviceManager.suspend(captureAny)).captured.single,
        suspendFlagReboot);
  });

  test('Shutdown should call device poweroff', () {
    statusModel.shutdownDevice();
    expect(verify(deviceManager.suspend(captureAny)).captured.single,
        suspendFlagPoweroff);
  });

  test('Logout should invoke onLogout callback', () {
    statusModel.logoutSession();
    expect(logoutCalled, true);
  });

  test('Reset should reset detail UiStream', () {
    final detailUiStream = MockUiStream();
    statusModel.detailNotifier.value = detailUiStream;
    statusModel.reset();
    verify(detailUiStream.update(any)).called(1);
  });
}

class MockUiStream extends Mock implements UiStream {}

class MockAdministratorProxy extends Mock implements AdministratorProxy {}

class MockProxyController extends Mock
    implements AsyncProxyController<Administrator> {}
