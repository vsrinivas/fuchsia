// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../metrics_results.dart';
import '../metrics_spec.dart';
import '../trace_model.dart';
import 'common.dart';

class _Results {
  List<double> renderFrameDurations;
}

_Results _scenicFrameStats(Model model) {
  final renderFrameDurations = filterEventsTyped<DurationEvent>(
          getAllEvents(model),
          category: 'gfx',
          name: 'RenderFrame')
      .map((e) => e.duration.toMillisecondsF())
      .toList();

  return _Results()..renderFrameDurations = renderFrameDurations;
}

List<TestCaseResults> scenicFrameStatsMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  final results = _scenicFrameStats(model);
  if (results.renderFrameDurations.isEmpty) {
    results.renderFrameDurations = [0.0];
  }

  return [
    TestCaseResults(
        'scenic_RenderFrame', Unit.milliseconds, results.renderFrameDurations),
  ];
}

String scenicFrameStatsReport(Model model) {
  final buffer = StringBuffer()..write('''
===
Scenic Frame Stats
===

''');

  final results = _scenicFrameStats(model);

  buffer
    ..write('render_frame_durations:\n')
    ..write(describeValues(results.renderFrameDurations, indent: 2));

  return buffer.toString();
}
