// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety in the imported files and
// remove this language version.
// @dart=2.9

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

class _Results {
  List<double> presentLatencyValuesMillis;
}

_Results _flatlandLatency(Model model) {
  final latencyValues =
      getEventToVsyncLatencyValues(model, 'gfx', 'FlatlandApp::PresentBegin');
  if (latencyValues.isEmpty) {
    // TODO: In the future, we could look into allowing clients to specify
    // whether this case should throw or not.  For the moment, we mirror the
    // behavior of "process_input_latency_trace.go", and throw here.
    throw ArgumentError('Computed 0 total flatland latency values');
  }

  return _Results()..presentLatencyValuesMillis = latencyValues;
}

List<TestCaseResults> flatlandLatencyMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final results = _flatlandLatency(model);

  return [
    TestCaseResults('flatland_vsync_latency', Unit.milliseconds,
        results.presentLatencyValuesMillis),
  ];
}

String flatlandLatencyReport(Model model) {
  final buffer = StringBuffer()..write('''
===
Flatland Latency
===

''');
  final results = _flatlandLatency(model);
  buffer
    ..write('flatland_vsync_latency (ms):\n')
    ..write(describeValues(results.presentLatencyValuesMillis, indent: 2))
    ..write('\n');

  return buffer.toString();
}
