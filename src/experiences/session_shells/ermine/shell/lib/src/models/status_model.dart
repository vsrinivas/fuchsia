// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:fidl_fuchsia_bluetooth_control/fidl_async.dart' as bluetooth;
import 'package:fidl_fuchsia_memory/fidl_async.dart' as mem;
import 'package:fidl_fuchsia_modular/fidl_async.dart' as modular;
import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fidl_fuchsia_power/fidl_async.dart' as power;
import 'package:fuchsia_services/services.dart';

import '../utils/utils.dart';
import '../widgets/status/status_graph_visualizer.dart';
import '../widgets/status/status_progress_bar_visualizer.dart';

// Model that manages the Status menu state of this session shell.
class StatusModel extends ChangeNotifier implements Inspectable {
  // TODO(FL-272): Mock data for visualization.
  // Data values
  final String _networkValue = 'WIRELESS';
  final String _networkDescriptor = 'STRONG SIGNAL';
  final String _cpuValue = '35%';
  final double _cpuMax = 100;
  String _memValue;
  double _memFill;
  double _memMax;
  String _batteryText;
  double _batteryLevel;
  final double _batteryMax = 100;
  final String _tasksValue = '13, 1 THR';
  final String _tasksDescriptor = '1 RUNNING';
  final String _weatherValue = '16°';
  final String _weatherDescriptor = 'SUNNY';
  final int _fpsValue = 120;
  // Service values
  StartupContext startupContext;
  final bluetooth.ControlProxy bluetoothControl;
  final modular.PuppetMasterProxy puppetMaster;
  static const Duration _systemInformationUpdatePeriod =
      Duration(milliseconds: 500);
  // memory monitor
  final mem.MonitorProxy statusMemoryService;
  final _memoryServiceBinding = mem.WatcherBinding();
  // battery manager
  final power.BatteryManagerProxy statusBatteryManager;
  final _batteryInfoWatcherBinding = power.BatteryInfoWatcherBinding();
  // Visualizer models
  StatusProgressBarVisualizerModel memoryModel;
  StatusProgressBarVisualizerModel batteryModel;
  StatusProgressBarVisualizerModel dummyVolumeModel;
  StatusProgressBarVisualizerModel dummyBrightnessModel;
  StatusGraphVisualizerModel dummyCpuModel;

  /// The [GlobalKey] associated with [Status] widget.
  final GlobalKey key = GlobalKey(debugLabel: 'status');

  StatusModel({
    this.statusMemoryService,
    this.statusBatteryManager,
    this.puppetMaster,
    this.bluetoothControl,
  }) {
    statusMemoryService
        .watch(_memoryServiceBinding.wrap(_MonitorWatcherImpl(this)));
    memoryModel = StatusProgressBarVisualizerModel(
      barFirst: true,
      barHeight: 14,
      barSize: .5,
      offset: 0,
    );
    statusBatteryManager
      ..watch(_batteryInfoWatcherBinding.wrap(_BatteryInfoWatcherImpl(this)))
      ..getBatteryInfo().then(_updateBattery);
    batteryModel = StatusProgressBarVisualizerModel(
      barFirst: true,
      barHeight: 14,
      barSize: .5,
      offset: 0,
    );
    dummyVolumeModel = StatusProgressBarVisualizerModel(
        barFill: 3,
        barFirst: false,
        barHeight: 14,
        barMax: 10,
        barSize: .7,
        barValue: '03',
        offset: 0);
    dummyBrightnessModel = StatusProgressBarVisualizerModel(
        barFill: 11,
        barFirst: false,
        barHeight: 14,
        barMax: 15,
        barSize: .7,
        barValue: '11',
        offset: 0);
    dummyCpuModel = StatusGraphVisualizerModel(
      graphHeight: 14,
      graphMax: getCpuMax(),
      graphMin: getCpuFill(),
      graphWidth: 143.3,
      borderActive: true,
      fillActive: true,
      graphFirst: true,
    );
    Timer.periodic(_systemInformationUpdatePeriod, (_) {
      _updateCPU();
    });
  }

  factory StatusModel.fromStartupContext(StartupContext startupContext) {
    final bluetoothControl = bluetooth.ControlProxy();
    startupContext.incoming.connectToService(bluetoothControl);
    final puppetMaster = modular.PuppetMasterProxy();
    startupContext.incoming.connectToService(puppetMaster);
    final statusMemoryService = mem.MonitorProxy();
    startupContext.incoming.connectToService(statusMemoryService);
    final statusBatteryManager = power.BatteryManagerProxy();
    startupContext.incoming.connectToService(statusBatteryManager);

    return StatusModel(
      statusMemoryService: statusMemoryService,
      statusBatteryManager: statusBatteryManager,
      puppetMaster: puppetMaster,
      bluetoothControl: bluetoothControl,
    );
  }

  // Launch settings mod.
  void launchSettings() {
    final storyMaster = modular.StoryPuppetMasterProxy();
    puppetMaster.controlStory('settings', storyMaster.ctrl.request());
    final addMod = modular.AddMod(
      intent: modular.Intent(
          action: '',
          handler: 'fuchsia-pkg://fuchsia.com/settings#meta/settings.cmx'),
      surfaceParentModName: [],
      modName: ['root'],
      surfaceRelation: modular.SurfaceRelation(),
    );
    storyMaster
      ..enqueue([modular.StoryCommand.withAddMod(addMod)])
      ..execute();
  }

  void refreshBluetoothDevices() async {
    final remoteDevices = await bluetoothControl.getKnownRemoteDevices();
    await Future.forEach(remoteDevices, (device) async {
      await bluetoothControl.connect(device);
    });
  }

  // Date
  String getDate() {
    StringBuffer constructedDate = StringBuffer('');
    DateTime currentTime = DateTime.now();
    constructedDate.write(
        '${_currentDay(currentTime.weekday)}, ${_currentMonth(currentTime.month)} ${currentTime.day} ${currentTime.year}');
    return constructedDate.toString();
  }

  // Network
  String getNetwork() => '$_networkValue / $_networkDescriptor';

  // CPU Usage
  String getCpu() => '$_cpuValue';
  double getCpuFill() => 0;
  double getCpuMax() => _cpuMax;
  double getCpuData() => Random().nextDouble() * 100;

  // Memory Usage
  @override
  void dispose() {
    statusMemoryService.ctrl.close();
    statusBatteryManager.ctrl.close();
    puppetMaster.ctrl.close();
    bluetoothControl.ctrl.close();
    super.dispose();
  }

  // Tasks
  String getTasks() => '$_tasksValue; $_tasksDescriptor';

  // Weather
  String getWeather() => '$_weatherValue / $_weatherDescriptor';

  // FPS
  String getFps() => _fpsValue.toString();

  // Helpers & Services
  String _currentDay(int day) {
    switch (day) {
      case 1:
        return 'MONDAY';
      case 2:
        return 'TUESDAY';
      case 3:
        return 'WEDNESDAY';
      case 4:
        return 'THURSDAY';
      case 5:
        return 'FRIDAY';
      case 6:
        return 'SATURDAY';
      case 7:
        return 'SUNDAY';
    }
    return '';
  }

  String _currentMonth(int month) {
    switch (month) {
      case 1:
        return 'JANUARY';
      case 2:
        return 'FEBRUARY';
      case 3:
        return 'MARCH';
      case 4:
        return 'APRIL';
      case 5:
        return 'MAY';
      case 6:
        return 'JUNE';
      case 7:
        return 'JULY';
      case 8:
        return 'AUGUST';
      case 9:
        return 'SEPTEMBER';
      case 10:
        return 'OCTOBER';
      case 11:
        return 'NOVEMBER';
      case 12:
        return 'DECEMBER';
    }
    return '';
  }

  void _updateMem(mem.Stats stats) {
    int memTotal = (stats.totalBytes);
    int memUsed = (memTotal - stats.freeBytes);
    String memGBTotal = _bytesToGB(memTotal);
    String memGBUsed = _bytesToGB(memUsed);
    _memValue = '${memGBUsed}GB / ${memGBTotal}GB';
    memoryModel.barValue = _memValue;
    _memFill = memUsed.toDouble();
    memoryModel.barFill = _memFill;
    _memMax = memTotal.toDouble();
    memoryModel.barMax = _memMax;
  }

  String _bytesToGB(int bytes) {
    return (bytes / pow(1024, 3)).toStringAsPrecision(3);
  }

  void _updateBattery(power.BatteryInfo info) {
    if (info.status == power.BatteryStatus.ok) {
      _batteryLevel = info.levelPercent;
      String chargeState = _chargeStatusToText(info.chargeStatus);
      _batteryText = '${_batteryLevel.toStringAsFixed(0)}% $chargeState';
      batteryModel
        ..barValue = _batteryText
        ..barFill = _batteryLevel
        ..barMax = _batteryMax; // 100%
    }
  }

  String _chargeStatusToText(power.ChargeStatus chargeStatus) {
    switch (chargeStatus) {
      case power.ChargeStatus.charging:
        return ': charging';
      case power.ChargeStatus.discharging:
        return ': discharging';
      default:
        return '';
    }
  }

  void _updateCPU() {
    double newGraphData = getCpuData();
    dummyCpuModel.graphData = newGraphData;
    String dummyCpuValue = newGraphData.toInt().toString();
    dummyCpuModel.graphValue = '$dummyCpuValue%';
  }

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

class _MonitorWatcherImpl extends mem.Watcher {
  final StatusModel status;
  _MonitorWatcherImpl(this.status);

  @override
  Future<void> onChange(mem.Stats stats) async {
    status._updateMem(stats);
  }
}

class _BatteryInfoWatcherImpl extends power.BatteryInfoWatcher {
  final StatusModel status;
  _BatteryInfoWatcherImpl(this.status);

  @override
  Future<void> onChangeBatteryInfo(power.BatteryInfo info) async {
    status._updateBattery(info);
  }
}
