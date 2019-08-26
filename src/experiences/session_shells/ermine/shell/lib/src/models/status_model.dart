// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:fidl_fuchsia_memory/fidl_async.dart' as mem;
import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart';
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
  final String _tasksValue = '13, 1 THR';
  final String _tasksDescriptor = '1 RUNNING';
  final String _weatherValue = '16°';
  final String _weatherDescriptor = 'SUNNY';
  final String _batteryValue = '99%';
  final String _batteryDescriptor = '3:15 LEFT';
  final int _fpsValue = 120;
  // Service values
  StartupContext startupContext;
  final mem.MonitorProxy statusMemoryService;
  final _memoryServiceBinding = mem.WatcherBinding();
  static const Duration _systemInformationUpdatePeriod =
      Duration(milliseconds: 500);
  // Visualizer models
  StatusProgressBarVisualizerModel memoryModel;
  StatusProgressBarVisualizerModel dummyVolumeModel;
  StatusProgressBarVisualizerModel dummyBrightnessModel;
  StatusGraphVisualizerModel dummyCpuModel;

  /// The [GlobalKey] associated with [Status] widget.
  final GlobalKey key = GlobalKey(debugLabel: 'ask');

  StatusModel({this.statusMemoryService}) {
    statusMemoryService
        .watch(_memoryServiceBinding.wrap(_MonitorWatcherImpl(this)));
    memoryModel = StatusProgressBarVisualizerModel(
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
    final statusMemoryService = mem.MonitorProxy();
    startupContext.incoming.connectToService(statusMemoryService);
    return StatusModel(statusMemoryService: statusMemoryService);
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
    super.dispose();
  }

  // Tasks
  String getTasks() => '$_tasksValue; $_tasksDescriptor';

  // Weather
  String getWeather() => '$_weatherValue / $_weatherDescriptor';

  // Battery
  String getBattery() => '$_batteryValue - $_batteryDescriptor';

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

  void _updateCPU() {
    double newGraphData = getCpuData();
    dummyCpuModel.graphData = newGraphData;
    String dummyCpuValue = newGraphData.toInt().toString();
    dummyCpuModel.graphValue = '$dummyCpuValue%';
  }

  String _bytesToGB(int bytes) {
    return (bytes / pow(1024, 3)).toStringAsPrecision(3);
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
