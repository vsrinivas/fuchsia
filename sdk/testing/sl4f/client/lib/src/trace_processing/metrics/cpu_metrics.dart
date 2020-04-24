// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../time_delta.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('CpuMetricsProcessor');

/// In case of longevity assistant test, we do not want to send all the data points
/// because there would be too many. Only aggregated metrics (min, max, avg,
/// percentiles) would be sent. After the catapult conversion, it is unavoidable that
/// a bunch of cpu_min_min, cpu_min_max, cpu_min_avg would be generated, which will
/// not be used.
/// Special attention might be needed here if we migrate off of catapult.
const _aggregateMetricsOnly = 'aggregateMetricsOnly';

List<TestCaseResults> cpuMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final duration = getTotalTraceDuration(model);
  if (duration < TimeDelta.fromMilliseconds(1100)) {
    _log.info(
        'Trace duration (${duration.toMilliseconds()} milliseconds) is too short to provide CPU information');
    return [];
  }
  final cpuPercentages = getArgValuesFromEvents<double>(
      filterEventsTyped<CounterEvent>(getAllEvents(model),
          category: 'system_metrics', name: 'cpu_usage'),
      'average_cpu_percentage');
  _log.info('Average CPU Load: ${computeMean(cpuPercentages)}');
  final List<TestCaseResults> testCaseResults = [];
  if (extraArgs.containsKey(_aggregateMetricsOnly) &&
      extraArgs[_aggregateMetricsOnly]) {
    for (final percentile in [5, 25, 50, 75, 95]) {
      testCaseResults.add(TestCaseResults('cpu_p$percentile', Unit.percent,
          [computePercentile<double>(cpuPercentages, percentile)]));
    }
    testCaseResults.addAll([
      TestCaseResults('cpu_min', Unit.percent, [computeMin(cpuPercentages)]),
      TestCaseResults('cpu_max', Unit.percent, [computeMax(cpuPercentages)]),
      TestCaseResults(
          'cpu_average', Unit.percent, [computeMean(cpuPercentages)]),
    ]);
  } else {
    testCaseResults.add(
        TestCaseResults('CPU Load', Unit.percent, cpuPercentages.toList()));
  }
  return testCaseResults;
}
