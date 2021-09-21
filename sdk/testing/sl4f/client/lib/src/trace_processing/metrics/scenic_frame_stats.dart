// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

class _Results {
  // This measures the wall time of all Scenic CPU work needed to produce a
  // frame.
  List<double> renderFrameCpuDurations;
  // This measures the wall time of all Scenic CPU and GPU work needed to
  // produce a frame.
  List<double> renderFrameTotalDurations;
  // This measures the wall time of all ("gfx", "RenderFrame") events.  Note
  // that the event does not cover all of the time that it takes Scenic to
  // render a frame.
  List<double> renderFrameEventDurations;
}

_Results _scenicFrameStats(Model model) {
  final startRenderingEvents = filterEventsTyped<DurationEvent>(
      getAllEvents(model),
      category: 'gfx',
      name: 'ApplyScheduledSessionUpdates');

  final endCpuRenderingEvents =
      startRenderingEvents.map((DurationEvent durationEvent) {
    final followingEvents = filterEventsTyped<DurationEvent>(
        getFollowingEvents(durationEvent),
        category: 'gfx',
        name: 'RenderFrame');
    if (followingEvents.isEmpty) {
      return null;
    }
    return followingEvents.first;
  });

  final endTotalRenderingEvents =
      startRenderingEvents.map((DurationEvent durationEvent) {
    final followingEvents = filterEventsTyped<DurationEvent>(
        getFollowingEvents(durationEvent),
        category: 'gfx',
        name: 'Display::Fence::OnReady');
    if (followingEvents.isEmpty) {
      return null;
    }
    return followingEvents.first;
  });

  final renderFrameCpuDurations =
      Zip2Iterable<DurationEvent, DurationEvent, double>(
          startRenderingEvents,
          endCpuRenderingEvents,
          (startRenderingEvent, endRenderingEvent) => (endRenderingEvent ==
                  null)
              ? null
              : (endRenderingEvent.start +
                      endRenderingEvent.duration -
                      startRenderingEvent.start)
                  .toMillisecondsF()).where((delta) => delta != null).toList();

  final renderFrameTotalDurations =
      Zip2Iterable<DurationEvent, DurationEvent, double>(
          startRenderingEvents,
          endTotalRenderingEvents,
          (startRenderingEvent, endRenderingEvent) => (endRenderingEvent ==
                  null)
              ? null
              : (endRenderingEvent.start - startRenderingEvent.start)
                  .toMillisecondsF()).where((delta) => delta != null).toList();

  return _Results()
    ..renderFrameCpuDurations = renderFrameCpuDurations
    ..renderFrameTotalDurations = renderFrameTotalDurations
    ..renderFrameEventDurations = endCpuRenderingEvents
        .where((e) => e != null)
        .map((e) => e.duration.toMillisecondsF())
        .toList();
}

List<TestCaseResults> scenicFrameStatsMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final results = _scenicFrameStats(model);
  if (results.renderFrameCpuDurations.isEmpty) {
    results.renderFrameCpuDurations = [0.0];
  }
  if (results.renderFrameTotalDurations.isEmpty) {
    results.renderFrameTotalDurations = [0.0];
  }
  if (results.renderFrameEventDurations.isEmpty) {
    results.renderFrameEventDurations = [0.0];
  }

  return [
    TestCaseResults('scenic_render_frame_cpu', Unit.milliseconds,
        results.renderFrameCpuDurations),
    TestCaseResults('scenic_render_frame_total', Unit.milliseconds,
        results.renderFrameTotalDurations),
    TestCaseResults('scenic_RenderFrame', Unit.milliseconds,
        results.renderFrameEventDurations),
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
    ..write('render_frame_cpu_durations:\n')
    ..write(describeValues(results.renderFrameCpuDurations, indent: 2))
    ..write('render_frame_total_durations:\n')
    ..write(describeValues(results.renderFrameTotalDurations, indent: 2))
    ..write('("gfx", "RenderFrame") event durations:\n')
    ..write(describeValues(results.renderFrameEventDurations, indent: 2))
    ..write('\n');

  return buffer.toString();
}
