// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:intl/intl.dart';

final DateFormat _kTimeOnlyDateFormat = DateFormat('h:mm', 'en_US');
final DateFormat _kDateOnlyDateFormat = DateFormat('EEEE MMM d', 'en_US');
final DateFormat _kShortStringDateFormat = DateFormat('h:mm', 'en_US');
final DateFormat _kLongStringDateFormat = DateFormat('EEEE h:mm', 'en_US');
final DateFormat _kMeridiemOnlyFormat = DateFormat('a', 'en_US');

/// Creates time strings and notifies when they change.
class TimeStringer extends Listenable {
  final Set<VoidCallback> _listeners = <VoidCallback>{};
  Timer _timer;
  int _offsetMinutes = 0;

  /// [listener] will be called whenever [shortString] or [longString] have
  /// changed.
  @override
  void addListener(VoidCallback listener) {
    _listeners.add(listener);
    if (_listeners.length == 1) {
      _scheduleTimer();
    }
  }

  /// [listener] will no longer be called whenever [shortString] or [longString]
  /// have changed.
  @override
  void removeListener(VoidCallback listener) {
    _listeners.remove(listener);
    if (_listeners.isEmpty) {
      _timer?.cancel();
      _timer = null;
    }
  }

  /// Returns the time only (eg. '10:34').
  String get timeOnly => _kTimeOnlyDateFormat
      .format(
        DateTime.now(),
      )
      .toUpperCase();

  /// Returns the date only (eg. 'MONDAY AUG 3').
  String get dateOnly => _kDateOnlyDateFormat
      .format(
        DateTime.now(),
      )
      .toUpperCase();

  /// Returns a short version of the time (eg. '10:34').
  String get shortString =>
      _kShortStringDateFormat.format(DateTime.now()).toLowerCase();

  /// Returns a long version of the time including the day (eg. 'Monday 10:34').
  String get longString =>
      _kLongStringDateFormat.format(DateTime.now()).toLowerCase();

  /// Returns the meridiem (eg. 'AM')
  String get meridiem =>
      _kMeridiemOnlyFormat.format(DateTime.now()).toUpperCase();

  /// Returns the offset, in minutes.
  int get offsetMinutes => _offsetMinutes;

  set offsetMinutes(int offsetMinutes) {
    if (_offsetMinutes != offsetMinutes) {
      _offsetMinutes = offsetMinutes;
      _notifyListeners();
    }
  }

  void _scheduleTimer() {
    _timer?.cancel();
    _timer =
        Timer(Duration(seconds: 61 - DateTime.now().second), () {
      _notifyListeners();
      _scheduleTimer();
    });
  }

  void _notifyListeners() {
    for (VoidCallback listener in _listeners.toList()) {
      listener();
    }
  }
}
