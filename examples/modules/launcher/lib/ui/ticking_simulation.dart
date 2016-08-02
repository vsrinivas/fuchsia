// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import 'rk4_spring_simulation.dart';

class TickingSimulation {
  final RK4SpringSimulation simulation;
  final VoidCallback onTick;

  Ticker _ticker;
  Duration _lastTick;

  TickingSimulation({this.simulation, this.onTick});

  void start() {
    _startTicking();
  }

  void stop() {
    _ticker?.stop();
    _ticker = null;
  }

  double get value => simulation.value;

  set target(double target) {
    simulation.target = target;
    start();
  }

  /// Called once per tick with [elapsedSeconds] indicating how long it's
  /// been since the last tick.
  bool handleTick(double elapsedSeconds) {
    simulation.elapseTime(elapsedSeconds);
    if (onTick != null) {
      onTick();
    }
    return !simulation.isDone;
  }

  void _startTicking() {
    if (_ticker?.isTicking ?? false) {
      return;
    }
    _ticker = new Ticker(_onTick);
    _lastTick = Duration.ZERO;
    _ticker.start();
  }

  void _onTick(Duration elapsed) {
    final double elapsedSeconds =
        (elapsed.inMicroseconds - _lastTick.inMicroseconds) / 1000000.0;
    _lastTick = elapsed;

    bool continueTicking = handleTick(elapsedSeconds);

    if (!continueTicking) {
      _ticker?.stop();
      _ticker = null;
    }
  }
}
