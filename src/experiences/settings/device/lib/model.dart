// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_update/fidl_async.dart' as update;
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart'
    as channelcontrol;
import 'package:fidl_fuchsia_recovery/fidl_async.dart' as recovery;
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:lib.settings/device_info.dart';
import 'package:lib.widgets/model.dart';
import 'package:zircon/zircon.dart';

/// Clock ID of the system monotonic clock, which measures uptime in nanoseconds.
const int _zxClockMonotonic = 0;

const Duration _uptimeRefreshInterval = Duration(seconds: 1);

/// An interface for abstracting system interactions.
abstract class SystemInterface {
  int get currentTime;

  Future<bool> checkForSystemUpdate();

  Future<String> getCurrentChannel();

  Future<void> setTargetChannel(String channel);

  Future<List<String>> getChannelList();

  Future<void> factoryReset();

  void dispose();
}

class DefaultSystemInterfaceImpl implements SystemInterface {
  /// Controller for the update manager service.
  final update.ManagerProxy _updateManager = update.ManagerProxy();

  /// Controller for the update channel control service.
  final channelcontrol.ChannelControlProxy _channelControl =
      channelcontrol.ChannelControlProxy();

  /// Controller for the factory reset service.
  final recovery.FactoryResetProxy _factoryReset = recovery.FactoryResetProxy();

  DefaultSystemInterfaceImpl() {
    StartupContext.fromStartupInfo().incoming.connectToService(_updateManager);
    StartupContext.fromStartupInfo().incoming.connectToService(_channelControl);
  }

  @override
  int get currentTime => System.clockGet(_zxClockMonotonic) ~/ 1000;

  @override
  Future<bool> checkForSystemUpdate() async {
    final options = update.Options(
      initiator: update.Initiator.user,
    );
    final status = await _updateManager.checkNow(options, null);
    return status != update.CheckStartedResult.throttled;
  }

  @override
  Future<String> getCurrentChannel() => _channelControl.getCurrent();

  @override
  Future<void> setTargetChannel(String channel) =>
      _channelControl.setTarget(channel);

  @override
  Future<List<String>> getChannelList() => _channelControl.getTargetList();

  @override
  Future<void> factoryReset() async {
    if (_factoryReset.ctrl.isUnbound) {
      StartupContext.fromStartupInfo().incoming.connectToService(_factoryReset);
    }

    await _factoryReset.reset();
  }

  @override
  void dispose() {
    _updateManager.ctrl.close();
    _channelControl.ctrl.close();
    _factoryReset.ctrl.close();
  }
}

/// Model containing state needed for the device settings app.
class DeviceSettingsModel extends Model {
  /// Placeholder time of last update, used to provide visual indication update
  /// was called.
  ///
  /// This will be removed when we have a more reliable way of showing update
  /// status.
  /// TODO: replace with better status info from update service
  DateTime _lastUpdate;

  /// Holds the build tag if a release build, otherwise holds
  /// the time the source code was updated.
  String _buildTag;

  /// Holds the time the source code was updated.
  String _sourceDate;

  /// Length of time since system bootup.
  Duration _uptime;
  Timer _uptimeRefreshTimer;

  bool _started = false;

  bool _showResetConfirmation = false;

  ValueNotifier<bool> channelPopupShowing = ValueNotifier<bool>(false);

  bool _isChannelUpdating = false;

  SystemInterface _sysInterface;

  DeviceSettingsModel(this._sysInterface);

  DeviceSettingsModel.withDefaultSystemInterface()
      : this(DefaultSystemInterfaceImpl());

  DateTime get lastUpdate => _lastUpdate;

  String _currentChannel;

  String get currentChannel => _currentChannel;

  List<String> _channels;

  List<String> get channels => _channels;

  /// Determines whether the confirmation dialog for factory reset should
  /// be displayed.
  bool get showResetConfirmation => _showResetConfirmation;

  /// Returns true if we are in the middle of updating the channel. Returns
  /// false otherwise.
  bool get channelUpdating => _isChannelUpdating;

  String get buildTag => _buildTag;

  String get sourceDate => _sourceDate;

  Duration get uptime => _uptime;

  bool get updateCheckDisabled =>
      DateTime.now().isAfter(_lastUpdate.add(Duration(seconds: 60)));

  /// Checks for update from the update service
  Future<void> checkForUpdates() async {
    await _sysInterface.checkForSystemUpdate();
    _lastUpdate = DateTime.now();
  }

  Future<void> selectChannel(String selectedChannel) async {
    log.info('selecting channel $selectedChannel');
    channelPopupShowing.value = false;
    _setChannelState(updating: true);

    await _sysInterface.setTargetChannel(selectedChannel);

    await _update();
  }

  Future<void> _update() async {
    _setChannelState(updating: true);

    _currentChannel = await _sysInterface.getCurrentChannel();

    _channels = await _sysInterface.getChannelList();

    _setChannelState(updating: false);
  }

  void _setChannelState({bool updating}) {
    if (_isChannelUpdating == updating) {
      return;
    }

    _isChannelUpdating = updating;
    notifyListeners();
  }

  void dispose() {
    _sysInterface.dispose();
    _uptimeRefreshTimer.cancel();
  }

  Future<void> start() async {
    if (_started) {
      return;
    }

    _started = true;
    _buildTag = DeviceInfo.buildTag;
    _sourceDate = DeviceInfo.sourceDate;

    updateUptime();
    _uptimeRefreshTimer =
        Timer.periodic(_uptimeRefreshInterval, (_) => updateUptime());

    await _update();

    channelPopupShowing.addListener(notifyListeners);
  }

  void updateUptime() {
    // System clock returns time since boot in nanoseconds.
    _uptime = Duration(microseconds: _sysInterface.currentTime);
    notifyListeners();
  }

  Future<void> factoryReset() async {
    if (showResetConfirmation) {
      log.warning('Triggering factory reset');
      await _sysInterface.factoryReset();
    } else {
      _showResetConfirmation = true;
      notifyListeners();
    }
  }

  void cancelFactoryReset() {
    _showResetConfirmation = false;
    notifyListeners();
  }
}
