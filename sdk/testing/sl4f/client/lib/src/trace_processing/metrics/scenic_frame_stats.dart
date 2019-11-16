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

List<TestCaseResults> scenicFrameStatsMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  final framePresentedEvents = filterEventsTyped<InstantEvent>(
          getAllEvents(model),
          category: 'gfx',
          name: 'FramePresented')
      .toList();
  final fps = (framePresentedEvents.length > 1)
      ? _legacyCalculateFpsForEvents(framePresentedEvents)
      : 0.0;

  var renderFrameDurations = filterEventsTyped<DurationEvent>(
          getAllEvents(model),
          category: 'gfx',
          name: 'RenderFrame')
      .map((e) => e.duration.toMillisecondsF())
      .toList();
  if (renderFrameDurations.isEmpty) {
    renderFrameDurations = [0.0];
  }

  return [
    TestCaseResults('scenic_fps', Unit.framesPerSecond, [fps]),
    TestCaseResults(
        'scenic_RenderFrame', Unit.milliseconds, renderFrameDurations)
  ];
}
