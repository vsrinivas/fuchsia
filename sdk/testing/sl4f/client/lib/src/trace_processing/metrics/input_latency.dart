// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

class _Results {
  List<double> inputLatencyValuesMillis;
}

_Results _inputLatency(Model model) {
  final inputEvents = filterEventsTyped<DurationEvent>(getAllEvents(model),
      category: 'input', name: 'presentation_on_event');
  final vsyncEvents = inputEvents.map(findFollowingVsync);

  final latencyValues = Zip2Iterable<DurationEvent, DurationEvent, double>(
          inputEvents,
          vsyncEvents,
          (inputEvent, vsyncEvent) => (vsyncEvent == null)
              ? null
              : (vsyncEvent.start - inputEvent.start).toMillisecondsF())
      .where((delta) => delta != null)
      .toList();

  if (latencyValues.isEmpty) {
    // TODO: In the future, we could look into allowing clients to specify
    // whether this case should throw or not.  For the moment, we mirror the
    // behavior of "process_input_latency_trace.go", and throw here.
    throw ArgumentError('Computed 0 total input latency values');
  }

  return _Results()..inputLatencyValuesMillis = latencyValues;
}

List<TestCaseResults> inputLatencyMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final results = _inputLatency(model);

  return [
    TestCaseResults('total_input_latency', Unit.milliseconds,
        results.inputLatencyValuesMillis),
  ];
}

String inputLatencyReport(Model model) {
  final buffer = StringBuffer()..write('''
===
Input Latency
===

''');
  final results = _inputLatency(model);
  buffer
    ..write('total_input_latency (ms):\n')
    ..write(describeValues(results.inputLatencyValuesMillis, indent: 2))
    ..write('\n');

  return buffer.toString();
}
