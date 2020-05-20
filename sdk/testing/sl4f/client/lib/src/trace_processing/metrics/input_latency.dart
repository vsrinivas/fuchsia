// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

List<TestCaseResults> inputLatencyMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
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

  return [
    TestCaseResults('total_input_latency', Unit.milliseconds, latencyValues),
  ];
}
