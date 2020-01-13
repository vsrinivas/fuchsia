// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_device_manager/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_services/services.dart';
import 'package:quickui/uistream.dart';
import 'package:settings/settings.dart';

import '../utils/utils.dart';

class StatusModel implements Inspectable {
  /// The [GlobalKey] associated with [Status] widget.
  final GlobalKey key = GlobalKey(debugLabel: 'status');
  UiStream brightness;
  UiStream memory;
  UiStream battery;
  UiStream weather;
  UiStream volume;
  UiStream bluetooth;
  UiStream datetime;
  UiStream timezone;
  final StartupContext startupContext;
  final AdministratorProxy deviceManager;
  final VoidCallback logout;
  final ValueNotifier<UiStream> detailNotifier = ValueNotifier<UiStream>(null);

  StatusModel({this.startupContext, this.deviceManager, this.logout}) {
    datetime = UiStream(Datetime());
    timezone = UiStream(TimeZone.fromStartupContext(startupContext));
    brightness = UiStream(Brightness.fromStartupContext(startupContext));
    memory = UiStream(Memory.fromStartupContext(startupContext));
    battery = UiStream(Battery.fromStartupContext(startupContext));
    volume = UiStream(Volume.fromStartupContext(startupContext));
    bluetooth = UiStream(Bluetooth.fromStartupContext(startupContext));
    weather = UiStream(Weather());
  }

  factory StatusModel.fromStartupContext(
      StartupContext startupContext, VoidCallback logout) {
    final deviceManager = AdministratorProxy();
    startupContext.incoming.connectToService(deviceManager);

    return StatusModel(
      startupContext: startupContext,
      deviceManager: deviceManager,
      logout: logout,
    );
  }

  void dispose() {
    deviceManager.ctrl.close();
    brightness.dispose();
    memory.dispose();
    battery.dispose();
    weather.dispose();
    volume.dispose();
    battery.dispose();
    datetime.dispose();
    timezone.dispose();
  }

  UiStream get detailStream => detailNotifier.value;

  void reset() {
    // Send [QuickAction.cancel] to the detail stream if on detail view.
    detailNotifier.value?.update(Value.withButton(ButtonValue(
      label: '',
      action: QuickAction.cancel.$value,
    )));
    detailNotifier.value = null;
  }

  /// Reboot the device.
  void restartDevice() => deviceManager.suspend(suspendFlagReboot);

  /// Shutdown the device.
  void shutdownDevice() => deviceManager.suspend(suspendFlagPoweroff);

  void logoutSession() => logout();

  @override
  void onInspect(Node node) {
    if (key.currentContext != null) {
      final rect = rectFromGlobalKey(key);
      if (rect == null) {
        return;
      }
      node
          .stringProperty('rect')
          .setValue('${rect.left},${rect.top},${rect.width},${rect.height}');
    } else {
      node.delete();
    }
  }
}
