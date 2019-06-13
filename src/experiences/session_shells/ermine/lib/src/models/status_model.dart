// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/foundation.dart';

/// Model that manages the Status menu state of this session shell.
class StatusModel extends ChangeNotifier {
  // TODO(FL-272): Mock data for visualization.
  final String _wirelessValue = 'wireless';
  final String _wirelessDescriptor = 'strong signal';
  final String _cpuValue = '35%';
  final double _cpuFill = 35;
  final double _cpuMax = 100;
  final String _memValue = '7.6G / 16.1G';
  final double _memFill = 7.6;
  final double _memMax = 16.1;
  final String _tasksValue = '13, 1 thr';
  final String _tasksDescriptor = '1 running';
  final String _weatherValue = '16°';
  final String _weatherDescriptor = 'Sunny';
  final String _batteryValue = '99%';
  final String _batteryDescriptor = '3:15 left';

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
}
