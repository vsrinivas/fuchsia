// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_modular/fidl_async.dart' as modular;
import 'package:fidl_fuchsia_device_manager/fidl_async.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_services/services.dart';
import 'package:quickui/uistream.dart';
import 'package:settings/settings.dart';

import '../utils/utils.dart';

const _kSettingsPackageUrl =
    'fuchsia-pkg://fuchsia.com/settings#meta/settings.cmx';

class StatusModel implements Inspectable {
  /// The [GlobalKey] associated with [Status] widget.
  final GlobalKey key = GlobalKey(debugLabel: 'status');
  UiStream brightness;
  UiStream memory;
  UiStream battery;
  final StartupContext startupContext;
  final modular.PuppetMasterProxy puppetMaster;
  final AdministratorProxy deviceManager;

  StatusModel({this.startupContext, this.puppetMaster, this.deviceManager}) {
    brightness = UiStream(Brightness.fromStartupContext(startupContext));
    memory = UiStream(Memory.fromStartupContext(startupContext));
    battery = UiStream(Battery.fromStartupContext(startupContext));
  }

  factory StatusModel.fromStartupContext(StartupContext startupContext) {
    final puppetMaster = modular.PuppetMasterProxy();
    startupContext.incoming.connectToService(puppetMaster);

    final deviceManager = AdministratorProxy();
    startupContext.incoming.connectToService(deviceManager);

    return StatusModel(
      startupContext: startupContext,
      puppetMaster: puppetMaster,
      deviceManager: deviceManager,
    );
  }

  void dispose() {
    deviceManager.ctrl.close();
    puppetMaster.ctrl.close();
    brightness.dispose();
    memory.dispose();
    battery.dispose();
  }

  /// Launch settings mod.
  void launchSettings() {
    final storyMaster = modular.StoryPuppetMasterProxy();
    puppetMaster.controlStory('settings', storyMaster.ctrl.request());
    final addMod = modular.AddMod(
      intent: modular.Intent(action: '', handler: _kSettingsPackageUrl),
      surfaceParentModName: [],
      modName: ['root'],
      surfaceRelation: modular.SurfaceRelation(),
    );
    storyMaster
      ..enqueue([modular.StoryCommand.withAddMod(addMod)])
      ..execute();
  }

  /// Reboot the device.
  void restartDevice() => deviceManager.suspend(suspendFlagReboot);

  /// Shutdown the device.
  void shutdownDevice() => deviceManager.suspend(suspendFlagPoweroff);

  @override
  void onInspect(Node node) {
    if (key.currentContext != null) {
      final rect = rectFromGlobalKey(key);
      node
          .stringProperty('rect')
          .setValue('${rect.left},${rect.top},${rect.width},${rect.height}');
    } else {
      node.delete();
    }
  }
}
