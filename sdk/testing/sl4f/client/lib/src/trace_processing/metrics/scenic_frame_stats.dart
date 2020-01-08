// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../metrics_results.dart';
import '../metrics_spec.dart';
import '../trace_model.dart';
import 'common.dart';

// TODO(39301): Implement the full "process_gfx_trace.go" version of this
//              logic.  We currently only implement the
//              "process_uiperf_trace.go" version.
double _legacyCalculateFpsForEvents(List<Event> fpsEvents) {
  fpsEvents.sort((a, b) => a.start.compareTo(b.start));
  final totalDuration =
      (fpsEvents.last.start - fpsEvents.first.start).toSecondsF();
  return fpsEvents.length / totalDuration;
}

class _Results {
  double averageFps;
  List<double> renderFrameDurations;
}

_Results _scenicFrameStats(Model model) {
  final framePresentedEvents = filterEventsTyped<InstantEvent>(
          getAllEvents(model),
          category: 'gfx',
          name: 'FramePresented')
      .toList();
  final fps = (framePresentedEvents.length > 1)
      ? _legacyCalculateFpsForEvents(framePresentedEvents)
      : 0.0;

  final renderFrameDurations = filterEventsTyped<DurationEvent>(
          getAllEvents(model),
          category: 'gfx',
          name: 'RenderFrame')
      .map((e) => e.duration.toMillisecondsF())
      .toList();

  return _Results()
    ..averageFps = fps
    ..renderFrameDurations = renderFrameDurations;
}

List<TestCaseResults> scenicFrameStatsMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  final results = _scenicFrameStats(model);
  if (results.renderFrameDurations.isEmpty) {
    results.renderFrameDurations = [0.0];
  }

  return [
    TestCaseResults('scenic_fps', Unit.framesPerSecond, [results.averageFps]),
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
    ..write('scenic_fps:\n')
    ..write('  ${results.averageFps}\n')
    ..write('\n')
    ..write('render_frame_durations:\n')
    ..write(describeValues(results.renderFrameDurations, indent: 2));

  return buffer.toString();
}
