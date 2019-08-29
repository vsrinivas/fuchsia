// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/physics.dart';
import 'package:flutter/widgets.dart';
import 'package:meta/meta.dart';

/// The base class for all generic immutable simulations.
abstract class Sim<T> {
  /// Construct with given tolerance.
  Sim({Tolerance tolerance})
      : _tolerance = tolerance ?? Tolerance.defaultTolerance;

  /// The output of the object in the simulation at the given time.
  T value(double time);

  /// The change in value of the object in the simulation at the given time.
  T velocity(double time);

  /// Whether the simulation is "done" at the given time.
  bool isDone(double time);

  /// How close to the actual end of the simulation a value at a particular time
  /// must be before [isDone] considers the simulation to be "done".
  ///
  /// A simulation with an asymptotic curve would never technically be "done",
  /// but once the difference from the value at a particular time and the
  /// asymptote itself could not be seen, it would be pointless to continue. The
  /// tolerance defines how to determine if the difference could not be seen.
  Tolerance get tolerance => _tolerance;
  final Tolerance _tolerance;

  @override
  String toString() => '$runtimeType';
}

/// Generates a Sim with given params.
typedef Simulate<T> = Sim<T> Function(T start, T end, T velocity);

// TODO(alangardner): Chaining operations

/// Convenience wrapper for Flutter simulation.
class SimDouble extends Sim<double> {
  /// Construct wrapper using a Simulation
  SimDouble({@required this.simulation})
      : super(tolerance: simulation.tolerance);

  /// The Simulation that is wrapped
  final Simulation simulation;

  @override
  double value(double time) => simulation.x(time);

  @override
  double velocity(double time) => simulation.dx(time);

  @override
  bool isDone(double time) => simulation.isDone(time);
}

/// A Simulation that never changes its value.
class StaticSimulation extends Simulation {
  /// Constructor with fixed value.
  StaticSimulation({@required double value}) : _value = value;

  final double _value;

  @override
  double x(double time) => _value;

  @override
  double dx(double time) => 0.0;

  @override
  bool isDone(double time) => true;
}

/// 2D Sim where each axis is independent of the other
class Independent2DSim extends Sim<Offset> {
  /// 2D Sim where the axis are indepenent simulations.
  Independent2DSim({
    @required this.xSim,
    @required this.ySim,
    Tolerance tolerance,
  }) : super(tolerance: tolerance);

  /// Convenience constructor when the simulation is symetric on each axis.
  Independent2DSim.symmetric({
    @required Simulation sim,
    Tolerance tolerance,
  })  : xSim = sim,
        ySim = sim,
        super(tolerance: tolerance);

  /// Convenience constructor when the value is fixed.
  Independent2DSim.static({@required Offset value})
      : xSim = StaticSimulation(value: value.dx),
        ySim = StaticSimulation(value: value.dy);

  /// The Simulation used for the x axis.
  final Simulation xSim;

  /// The Simulation used for the y axis.
  final Simulation ySim;

  @override
  Offset value(double time) => Offset(xSim.x(time), ySim.x(time));

  @override
  Offset velocity(double time) => Offset(xSim.dx(time), ySim.dx(time));

  @override
  bool isDone(double time) => xSim.isDone(time) && ySim.isDone(time);
}

/// Rect Sim with independent size and position simulators.
class IndependentRectSim extends Sim<Rect> {
  /// Constructor
  IndependentRectSim({
    @required this.sizeSim,
    @required this.positionSim,
    FractionalOffset origin,
    Tolerance tolerance,
  })  : origin = origin ?? FractionalOffset.center,
        super(tolerance: tolerance);

  /// The Size Sim
  final Sim<Offset> sizeSim;

  /// The Position Sim
  final Sim<Offset> positionSim;

  /// The Origin of The Rect
  final FractionalOffset origin;

  @override
  Rect value(double time) {
    Size size = Size.zero + sizeSim.value(time);
    return (positionSim.value(time) - origin.alongSize(size)) & size;
  }

  @override
  Rect velocity(double time) =>
      positionSim.velocity(time) & (Size.zero + sizeSim.velocity(time));

  @override
  bool isDone(double time) => positionSim.isDone(time) && sizeSim.isDone(time);
}
