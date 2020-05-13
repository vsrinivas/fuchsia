// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_cobalt/fidl_async.dart';

import '../utils/frame_rate_tracer.dart';
import '../widgets/rk4_spring_simulation.dart';
import 'spring_model.dart';

export 'spring_model.dart' show ScopedModel, Model, ScopedModelDescendant;

const RK4SpringDescription _kSimulationDesc =
    RK4SpringDescription(tension: 450.0, friction: 50.0);

/// Models the progress of a spring simulation.
class TracingSpringModel extends SpringModel {
  final FrameRateTracer _frameRateTracer;
  final Map<double, int> _targetToCobaltMetricIdMap;

  /// Constructor.
  TracingSpringModel({
    RK4SpringDescription springDescription = _kSimulationDesc,
    String traceName = 'TracingSpringModel',
    Logger cobaltLogger,
    Map<double, int> targetToCobaltMetricIdMap = const <double, int>{},
  })  : _frameRateTracer = FrameRateTracer(
          name: traceName,
          cobaltLogger: cobaltLogger,
        ),
        _targetToCobaltMetricIdMap = targetToCobaltMetricIdMap,
        super(springDescription: springDescription);

  @override
  set target(double target) {
    _frameRateTracer.start(
      targetName: '$target',
      cobaltMetricId: _targetToCobaltMetricIdMap[target],
    );
    super.target = target;
  }

  @override
  bool handleTick(double elapsedSeconds) {
    _frameRateTracer.tick();
    bool result = super.handleTick(elapsedSeconds);
    if (isDone) {
      _frameRateTracer.done();
    }
    return result;
  }
}
