// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('TemperatureMetricsProcessor');

/// In case of longevity assistant test, we do not want to send all the data points
/// because there would be too many. Only aggregated metrics (min, max, avg,
/// percentiles) would be sent. After the catapult conversion, it is unavoidable that
/// a bunch of temperature_min_min, temperature_min_max, temperature_min_avg would be
/// generated, which will not be used.
/// Special attention might be needed here if we migrate off of catapult.
const _aggregateMetricsOnly = 'aggregateMetricsOnly';

List<TestCaseResults> temperatureMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  // For soft-transition, try to get power data from temperature_logger, and
  // fall back to data from power_manager if it is not present.
  // TODO(fxbug.dev/66087): Remove the fallback to power_manager
  List<double> temperatureReadings = getArgValuesFromEvents<num>(
          filterEventsTyped<CounterEvent>(getAllEvents(model),
              category: 'temperature_logger', name: 'temperature'),
          'soc_pll')
      .map((t) => t.toDouble())
      .toList();
  if (temperatureReadings.isEmpty) {
    temperatureReadings = getArgValuesFromEvents<num>(
            filterEventsTyped<CounterEvent>(getAllEvents(model),
                category: 'power_manager',
                name: 'ThermalPolicy raw_temperature'),
            'raw_temperature')
        .map((t) => t.toDouble())
        .toList();
  }
  if (temperatureReadings.isEmpty) {
    final duration = getTotalTraceDuration(model);
    _log.info('No temperature readings are present. Perhaps the trace duration '
        '(${duration.toMilliseconds()} milliseconds) is too short to provide '
        'temperature information');
    return [];
  }

  _log.info(
      'Average temperature reading: ${computeMean(temperatureReadings)} degree Celsius');
  final List<TestCaseResults> testCaseResults = [];
  if (extraArgs.containsKey(_aggregateMetricsOnly) &&
      extraArgs[_aggregateMetricsOnly]) {
    for (final percentile in [5, 25, 50, 75, 95]) {
      testCaseResults.add(TestCaseResults(
          'temperature_p$percentile',
          Unit.count,
          [computePercentile<double>(temperatureReadings, percentile)]));
    }
    testCaseResults.addAll([
      TestCaseResults(
          'temperature_min', Unit.count, [computeMin(temperatureReadings)]),
      TestCaseResults(
          'temperature_max', Unit.count, [computeMax(temperatureReadings)]),
      TestCaseResults(
          'temperature_average', Unit.count, [computeMean(temperatureReadings)])
    ]);
  } else {
    testCaseResults.add(
      TestCaseResults(
          'Device temperature', Unit.count, temperatureReadings.toList()),
    );
  }
  return testCaseResults;
}
