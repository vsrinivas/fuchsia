// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('PowerMetricsProcessor');

/// In case of longevity assistant test, we do not want to send all the data
/// points because there would be too many. Only aggregated metrics (min, max,
/// avg, percentiles) would be sent. After the catapult conversion, it is
/// unavoidable that a bunch of power_min_min, power_min_max, power_min_avg
/// would be generated, which will not be used. Special attention might be
/// needed here if we migrate off of catapult.
const _aggregateMetricsOnly = 'aggregateMetricsOnly';

List<TestCaseResults> powerMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  // Get all the power trace events
  Iterable<CounterEvent> powerEvents = filterEventsTyped<CounterEvent>(
      getAllEvents(model),
      category: 'metrics_logger',
      name: 'power');
  if (powerEvents.isEmpty) {
    final duration = getTotalTraceDuration(model);
    _log.info('No power events are present. Perhaps the trace duration '
        '(${duration.toMilliseconds()} milliseconds) is too short to provide '
        'power information');
    return [];
  }

  // Convert the power events to a list of power values by summing together all
  // of the per-sensor power values contained in each event
  Iterable<double> powerValues = powerEvents.map((event) {
    // Sanity check: we expect the power event to contain a 'client_id' arg
    if (!event.args.containsKey('client_id')) {
      throw ArgumentError('Error, expected events to include key "client_id"');
    }

    // Extract the power readings from each sensor to a list of doubles
    final perSensorPowerValues = event.args.entries
        .where((arg) => arg.key != 'client_id')
        .map((arg) => arg.value.toDouble());
    if (perSensorPowerValues.isEmpty) {
      throw ArgumentError('Error, missing power values in trace event');
    }

    // Sum up the list of power readings and return
    return perSensorPowerValues.reduce((value, element) => value + element);
  });

  _log.info(
      'Average power reading: ${computeMean(powerValues).toStringAsFixed(3)} Watts');
  final List<TestCaseResults> testCaseResults = [];
  if (extraArgs.containsKey(_aggregateMetricsOnly) &&
      extraArgs[_aggregateMetricsOnly]) {
    // In a better world, we would not need to separately export percentiles of
    // the list of power values.  Unfortunately though, our performance metrics
    // dashboard is hard-coded to compute precisely the
    //  * count
    //  * maximum
    //  * mean of logs (i.e. mean([log(x) for x in xs]))
    //  * mean
    //  * min
    //  * sum
    //  * variance
    // of lists of values.  So instead, we compute percentiles and export them
    // as their own metrics, which contain lists of size 1. Unfortunately this
    // also means that useless statistics for the percentile metric will be
    // generated.
    //
    // If we ever switch to a performance metrics dashboard that supports
    // specifying what statistics to compute for a metric, then we should remove
    // these separate percentile metrics.
    for (final percentile in [5, 25, 50, 75, 95]) {
      testCaseResults.add(TestCaseResults('power_p$percentile', Unit.count,
          [computePercentile<double>(powerValues, percentile)]));
    }
    testCaseResults.addAll([
      TestCaseResults('power_min', Unit.count, [computeMin(powerValues)]),
      TestCaseResults('power_max', Unit.count, [computeMax(powerValues)]),
      TestCaseResults('power_average', Unit.count, [computeMean(powerValues)])
    ]);
  } else {
    testCaseResults.add(
      TestCaseResults('Device power', Unit.count, powerValues.toList()),
    );
  }
  return testCaseResults;
}
