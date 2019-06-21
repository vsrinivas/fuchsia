// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:fidl_fuchsia_memory/fidl_async.dart' as mem;
import 'package:flutter/foundation.dart';
import 'package:fuchsia_services/services.dart';

/// Model that manages the Status menu state of this session shell.
class StatusModel extends ChangeNotifier {
  // TODO(FL-272): Mock data for visualization.
  final String _wirelessValue = 'wireless';
  final String _wirelessDescriptor = 'strong signal';
  final String _cpuValue = '35%';
  final double _cpuFill = 35;
  final double _cpuMax = 100;
  String _memValue;
  double _memFill;
  double _memMax;
  final String _tasksValue = '13, 1 thr';
  final String _tasksDescriptor = '1 running';
  final String _weatherValue = '16°';
  final String _weatherDescriptor = 'Sunny';
  final String _batteryValue = '99%';
  final String _batteryDescriptor = '3:15 left';
  StartupContext startupContext;
  final mem.MonitorProxy statusMemoryService;
  final _memoryServiceBinding = mem.WatcherBinding();

  StatusModel({this.statusMemoryService}) {
    statusMemoryService.watch(_memoryServiceBinding.wrap(_MonitorWatcherImpl(this)));
  }

  factory StatusModel.fromStartupContext(StartupContext startupContext) {
    final statusMemoryService = mem.MonitorProxy();
    startupContext
          .incoming
          .connectToService(statusMemoryService);
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

  // Wireless
  String getWireless() => '$_wirelessValue / $_wirelessDescriptor';

  // CPU Usage
  String getCpu() => '$_cpuValue';
  double getCpuFill() => _cpuFill;
  double getCpuMax() => _cpuMax;

  // Memory Usage
  String getMem() => '$_memValue';
  double getMemFill() => _memFill;
  double getMemMax() => _memMax;

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

  // Helpers
  String _currentDay(int day) {
    switch (day) {
      case 1:
        return 'Monday';
      case 2:
        return 'Tuesday';
      case 3:
        return 'Wednesday';
      case 4:
        return 'Thursday';
      case 5:
        return 'Friday';
      case 6:
        return 'Saturday';
      case 7:
        return 'Sunday';
    }
    return '';
  }

  String _currentMonth(int month) {
    switch (month) {
      case 1:
        return 'January';
      case 2:
        return 'February';
      case 3:
        return 'March';
      case 4:
        return 'April';
      case 5:
        return 'May';
      case 6:
        return 'June';
      case 7:
        return 'July';
      case 8:
        return 'August';
      case 9:
        return 'September';
      case 10:
        return 'October';
      case 11:
        return 'November';
      case 12:
        return 'December';
    }
    return '';
  }

  void _onChange(mem.Stats stats) {
    int memTotal = (stats.totalBytes);
    int memUsed = (memTotal - stats.freeBytes);
    String memGBTotal = _bytesToGB(memTotal);
    String memGBUsed = _bytesToGB(memUsed);
    _memValue = '$memGBUsed / $memGBTotal GB';
    _memFill = memUsed.toDouble();
    _memMax = memTotal.toDouble();
  }

  String _bytesToGB(int bytes) {
    return (bytes / pow(1024, 3)).toStringAsPrecision(3);
  }
}

class _MonitorWatcherImpl extends mem.Watcher {
  final StatusModel status;
  _MonitorWatcherImpl(this.status);

  @override
  Future<void> onChange(mem.Stats stats) async {
    status._onChange(stats);
  }
}
