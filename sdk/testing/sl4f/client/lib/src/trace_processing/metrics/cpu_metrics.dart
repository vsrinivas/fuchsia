// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../metrics_spec.dart';
import '../time_delta.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('CpuMetricsProcessor');

List<TestCaseResults> cpuMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  if (metricsSpec.name != 'cpu') {
    throw ArgumentError(
        'Error, unexpected metrics name "${metricsSpec.name}" in '
        'cpuMetricsProcessor');
  }
  final duration = getTotalTraceDuration(model);
  if (duration < TimeDelta.fromMilliseconds(1100)) {
    _log.info(
        'Trace duration (${duration.toMilliseconds()} millisecconds) is too short to provide CPU information');
    return [];
  }
  final cpuPercentages = getArgsFromEvents<double>(
      filterEventsTyped<CounterEvent>(getAllEvents(model),
          category: 'system_metrics', name: 'cpu_usage'),
      'average_cpu_percentage');

  _log.info('Average CPU Load: ${computeMean(cpuPercentages)}');
  return [TestCaseResults('CPU Load', Unit.percent, cpuPercentages.toList())];
}
