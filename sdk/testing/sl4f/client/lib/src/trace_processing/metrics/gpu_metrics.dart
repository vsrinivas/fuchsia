// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../time_delta.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('GpuMetricsProcessor');

T _min<T extends Comparable<T>>(T a, T b) => a.compareTo(b) < 0 ? a : b;

List<TestCaseResults> gpuMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final gpuUtilizationEvents = filterEventsTyped<CounterEvent>(
          getAllEvents(model),
          category: 'magma',
          name: 'GPU Utilization')
      .toList();

  if (gpuUtilizationEvents.length < 2) {
    _log.warning('Unable to compute GPU utilization for only '
        '${gpuUtilizationEvents.length} events');
    return [];
  }

  gpuUtilizationEvents.sort((a, b) => a.start.compareTo(b.start));

  // In general, the GPU utilization events emitted by the driver will arrive at
  // irregular time intervals.  In order to recover meaningful summary
  // statistics, resample them into constant 1 second time intervals.
  //
  // So for example,
  //     [(20% utilization, 1.5 seconds), (80% utilization, 2.5 seconds)]
  // would become
  //     [20%, 50%, 80%, 80%]
  //

  final utilizationValues = <double>[];
  var runningValue = 0.0;
  var runningDuration = TimeDelta.zero();

  for (var i = 1; i < gpuUtilizationEvents.length; i++) {
    final a = gpuUtilizationEvents[i - 1];
    final b = gpuUtilizationEvents[i];
    if (!(b.args.containsKey('utilization') && b.args['utilization'] is num)) {
      throw ArgumentError(
          'Error, expected ("magma", "GPU Utilization") event to'
          'have key "utilization" in args, and instead found ${b.args}');
    }
    final double value = b.args['utilization'].toDouble();
    var duration = b.start - a.start;
    assert(duration > TimeDelta.zero());
    while (duration > TimeDelta.zero()) {
      assert(runningDuration < TimeDelta.fromSeconds(1));
      final durationToTake =
          _min(duration, TimeDelta.fromSeconds(1) - runningDuration);
      runningValue += durationToTake.toSecondsF() * value;
      runningDuration += durationToTake;
      if (runningDuration == TimeDelta.fromSeconds(1)) {
        utilizationValues.add(runningValue);
        runningValue = 0.0;
        runningDuration = TimeDelta.zero();
      }
      duration -= durationToTake;
    }
  }

  // In the case where we got less than 1 second of utilization counter values
  // (which should be rare), expand the sub-second window out into a single
  // value.  If we got more than 1 second of utilization counter values, then
  // just drop the last partial value.
  if (utilizationValues.isEmpty && runningDuration > TimeDelta.zero()) {
    assert(runningDuration < TimeDelta.fromSeconds(1));
    utilizationValues.add((1.0 / runningDuration.toSecondsF()) * runningValue);
  }

  assert(utilizationValues.isNotEmpty);

  // There's no proportion unit in catapult, so map the proportions to
  // percentages.
  final utilizationPercentages =
      utilizationValues.map((value) => 100.0 * value).toList();

  return [
    TestCaseResults('GPU Utilization', Unit.percent, utilizationPercentages),
  ];
}
