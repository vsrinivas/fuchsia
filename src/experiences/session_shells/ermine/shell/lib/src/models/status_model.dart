// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_device_manager/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_services/services.dart';
import 'package:quickui/uistream.dart';
import 'package:settings/settings.dart';

import '../utils/utils.dart';

class StatusModel implements Inspectable {
  /// The [GlobalKey] associated with [Status] widget.
  final GlobalKey key = GlobalKey(debugLabel: 'status');
  final UiStream brightness;
  final UiStream memory;
  final UiStream battery;
  final UiStream volume;
  final UiStream bluetooth;
  final UiStream datetime;
  final UiStream timezone;
  final UiStream channel;
  final UiStream systemInformation;
  final AdministratorProxy deviceManager;
  final VoidCallback logout;
  final ValueNotifier<UiStream> detailNotifier = ValueNotifier<UiStream>(null);
  final ValueNotifier<DateTime> currentTime =
      ValueNotifier<DateTime>(DateTime.now());

  StatusModel({
    this.datetime,
    this.timezone,
    this.brightness,
    this.memory,
    this.battery,
    this.volume,
    this.bluetooth,
    this.deviceManager,
    this.logout,
    this.channel,
    this.systemInformation,
  }) {
    Timer.periodic(
        Duration(seconds: 1), (_) => currentTime.value = DateTime.now());
  }

  factory StatusModel.withSvcPath(VoidCallback logout) {
    final deviceManager = AdministratorProxy();
    Incoming.fromSvcPath().connectToService(deviceManager);

    return StatusModel(
      datetime: UiStream(Datetime()),
      timezone: UiStream(TimeZone.withSvcPath()),
      brightness: UiStream(Brightness.withSvcPath()),
      memory: UiStream(Memory.withSvcPath()),
      battery: UiStream(Battery.withSvcPath()),
      volume: UiStream(Volume.withSvcPath()),
      bluetooth: UiStream(Bluetooth.withSvcPath()),
      deviceManager: deviceManager,
      logout: logout,
      channel: UiStream(Channel.withSvcPath()),
      systemInformation: UiStream(SystemInformation.withSvcPath()),
    );
  }

  void dispose() {
    deviceManager.ctrl.close();
    brightness.dispose();
    memory.dispose();
    battery.dispose();
    volume.dispose();
    datetime.dispose();
    timezone.dispose();
    bluetooth.dispose();
    channel.dispose();
    systemInformation.dispose();
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
