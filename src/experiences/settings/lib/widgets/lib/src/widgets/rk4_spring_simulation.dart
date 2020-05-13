// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

/// The settle theshold for the spring (for both distance and/or velocity).
const double _kTolerance = 0.01;

/// Spcifies the properties of a fourth order Runge-Kutta spring for
/// the [RK4SpringSimulation] .
class RK4SpringDescription {
  /// The tension of the spring.
  final double tension;

  //// The strength of the friction acting against the spring's movement.
  final double friction;

  /// The velocity the spring will start with.
  final double startingVelocity;

  /// Constructor.
  const RK4SpringDescription({
    this.tension = 300.0,
    this.friction = 25.0,
    this.startingVelocity = 0.0,
  });
}

/// Transitions values given to it via a fourth order Runge-Kutta spring
/// simulation.
/// [desc] specifies the properties of the spring.
class RK4SpringSimulation {
  /// The parameters of the simulation.
  final RK4SpringDescription desc;

  double _startValue;
  double _targetValue;

  /// The current velocity of the spring.
  double _velocity;

  /// The current acceleration multiplier.  This slowly grows as the simulation
  /// progresses.  This is used to make the spring more gentle by slowing its
  /// initial progress.
  double _accelerationMultipler;

  bool _isDone;

  /// How far along the spring the current value is, 0 being start and 1 being
  /// end. Because this is a spring that value will go beyond 1 and then back
  /// below 1 etc, as it 'springs'.
  double _curT = 0.0;

  double _delta;
  double _value;

  /// [initValue] is the value the simulation will begin with.
  /// [desc] specifies the parameters of the simulation and is optional.
  RK4SpringSimulation({
    double initValue = 0.0,
    this.desc = const RK4SpringDescription(),
  })  : _startValue = initValue,
        _targetValue = initValue,
        _value = initValue,
        _delta = 0.0,
        _velocity = 0.0,
        _accelerationMultipler = 0.0,
        _isDone = true;

  /// Sets a new [target] value for the simulation.
  set target(double target) {
    if (_targetValue != target) {
      // If we're flipping the spring we need to flip its velocity.
      bool wasGoingPositively = _targetValue > _startValue;
      bool willBeGoingPositively = target > value;
      if (wasGoingPositively != willBeGoingPositively) {
        _velocity = -_velocity;
      }
      _startValue = value;
      _targetValue = target;
      _delta = _targetValue - _startValue;
      if (_startValue != _targetValue) {
        _curT = 0.0;
        _isDone = false;
        _accelerationMultipler = 0.0;
      }
    }
  }

  /// You can only set velocity when the spring is in motion.
  set velocity(double velocity) {
    if (!_isDone) {
      _velocity = velocity;
    }
  }

  /// Returns true if the simulation is done - it's reached its target and has
  /// no velocity.
  bool get isDone => _isDone;

  /// The simulation's current value.
  double get value => _value;

  /// The simulation's target value.
  double get target => _targetValue;

  /// Runs the simulated variable for the given number of [seconds].
  void elapseTime(double seconds) {
    // If we're already where we need to be, do nothing.
    if (isDone) {
      return;
    }
    double secondsRemaining = seconds;
    const double _kMaxStepSize = 1 / 60;
    while (secondsRemaining > 0.0) {
      double stepSize =
          secondsRemaining > _kMaxStepSize ? _kMaxStepSize : secondsRemaining;
      _accelerationMultipler = math.min(
        1.0,
        _accelerationMultipler + stepSize * 6.0,
      );
      if (_evaluateRK(stepSize)) {
        _curT = 1.0;
        _value = _targetValue;
        velocity = 0.0;
        _isDone = true;
        _accelerationMultipler = 0.0;
        return;
      }
      secondsRemaining -= _kMaxStepSize;
    }
    _value = _startValue + _curT * _delta;
  }

  /// Evaluates one tick of a spring using the Runge-Kutta Algorithm.
  /// This is generally a bit mathy, here is a reasonable intro for those
  /// interested:
  ///   http://gafferongames.com/game-physics/integration-basics/
  /// [stepSize]: the duration between steps.
  ///   This should be around 1/60th of a second. Values too large will not work
  ///   as the spring adjusts itself over time based on previous values, so if
  ///   values are larger than 1/60th of a second this method should be called
  ///   for each whole 1/60th of a second segment plus the remainder.
  ///
  /// Returns true if the spring has settled to minimum amount.
  bool _evaluateRK(double stepSize) {
    double x = _curT - 1.0;
    double v = _velocity;

    double aDx = v;
    double aDv = _accelerateX(x, vel: v);

    double bDx = v + aDv * (stepSize * 0.5);
    double bDv = _accelerateX(x + aDx * (stepSize * 0.5), vel: bDx);

    double cDx = v + bDv * (stepSize * 0.5);
    double cDv = _accelerateX(x + bDx * (stepSize * 0.5), vel: cDx);

    double dDx = v + cDv * stepSize;
    double dDv = _accelerateX(x + cDx * stepSize, vel: dDx);

    double dxdt = 1.0 / 6.0 * (aDx + 2.0 * (bDx + cDx) + dDx);
    double dvdt = 1.0 / 6.0 * (aDv + 2.0 * (bDv + cDv) + dDv);
    double aftX = x + dxdt * stepSize;
    double aftV = v + dvdt * stepSize;

    _curT = 1 + aftX;
    double finalVelocity = aftV;
    double netFloat = aftX;
    double net1DVelocity = aftV;
    bool netValueIsLow = netFloat.abs() < _kTolerance;
    bool netVelocityIsLow = net1DVelocity.abs() < _kTolerance;
    _velocity = finalVelocity;
    // never turn spring back on
    return netValueIsLow && netVelocityIsLow;
  }

  double _accelerateX(double x, {double vel}) =>
      (-desc.tension * x - desc.friction * vel) * _accelerationMultipler;

  @override
  String toString() => 'RK4($value => $target)';
}
