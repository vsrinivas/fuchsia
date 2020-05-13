// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../widgets/rk4_spring_simulation.dart';
import 'ticking_model.dart';

export 'ticking_model.dart' show ScopedModel, Model, ScopedModelDescendant;

const RK4SpringDescription _kSimulationDesc =
    RK4SpringDescription(tension: 450.0, friction: 50.0);

/// Models the progress of a spring simulation.
class SpringModel extends TickingModel {
  /// The description of the spring the model uses.
  final RK4SpringDescription springDescription;

  RK4SpringSimulation _simulation;

  /// Constructor.
  SpringModel({this.springDescription = _kSimulationDesc}) {
    _simulation = RK4SpringSimulation(
      initValue: 0.0,
      desc: springDescription,
    );
  }

  /// Jumps the simulation to [value].
  void jump(double value) {
    _simulation = RK4SpringSimulation(
      initValue: value,
      desc: springDescription,
    );
    startTicking();
    notifyListeners();
  }

  /// Sets the new velocity for the simulation to [velocity].
  /// Note when setting this value, this velocity needs to be in the direction
  /// of the current target and should be from the frame of reference of going
  /// from 0.0 to 1.0 instead of the absolute values of value and target.
  /// Note also that you can only set velocity if the spring is in motion.
  set velocity(double velocity) {
    _simulation.velocity = velocity;
    startTicking();
  }

  /// Sets the new target for the simulation to [target].
  set target(double target) {
    if (_simulation.target != target) {
      _simulation.target = target;
      startTicking();
    }
  }

  /// The current simulation target.
  double get target => _simulation.target;

  /// The value of spring simulation.
  double get value => _simulation.value;

  /// Returns true if the spring simulation is in a steady state.
  bool get isDone => _simulation.isDone;

  @override
  bool handleTick(double elapsedSeconds) {
    _simulation.elapseTime(elapsedSeconds);
    return !_simulation.isDone;
  }
}
