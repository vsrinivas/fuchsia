// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../metrics_results.dart';
import '../metrics_spec.dart';
import '../time_delta.dart';
import '../time_point.dart';
import '../trace_model.dart';
import 'common.dart';

T _max<T extends Comparable<T>>(T a, T b) => (a.compareTo(b) >= 0) ? a : b;

/// Determine the display refresh rate in [model] by computing the average
/// interval between "VSYNC" events.
TimeDelta _computeDisplayRefreshRate(Model model) {
  // "VSYNC" events are already system-wide global, so don't bother with
  // hunting for any particular process/thread name.
  final vsyncs = filterEventsTyped<InstantEvent>(getAllEvents(model),
          category: 'gfx', name: 'VSYNC')
      .toList();

  if (vsyncs.length <= 1) {
    throw ArgumentError(
        'Trace model must contain at least 2 ("gfx", "VSYNC") events to compute'
        ' refresh rate, and instead found ${vsyncs.length} matching events');
  }

  vsyncs.sort((a, b) => a.start.compareTo(b.start));
  final vsyncTimestamps =
      vsyncs.map((vsync) => vsync.start.toEpochDelta().toNanosecondsF());
  final differencedVsyncTimestamps = differenceValues(vsyncTimestamps);
  final refreshRate = computeMean(differencedVsyncTimestamps);
  return TimeDelta.fromNanoseconds(refreshRate);
}

/// Groups of FPS events, where events in the same group are part of the same
/// batch of frames we attempted to render.
///
/// For example, in:
///   ["idle", "render frame 1", "render frame 2", "idle", "idle", "idle", "idle", "render frame 3", "idle"]
/// The groups are:
///   ["render frame 1", "render frame 2"]
///   ["render frame 3"]
/// because there is idle time in between "render frame 2" and "render frame 3".
class _Group {
  List<Event> events = [];
  // The time that this group of [events] "ends" at, which is most likely the
  // end timestamp of the last event, however could possibly be an assumed
  // VSYNC extended "vsync callback" to handle the edge case of a "vsync
  // callback" not being connected to a "VSYNC".  See the edge case handling
  // logic in [computeFps] below for further detail.
  TimePoint end = TimePoint.fromEpochDelta(TimeDelta.zero());
}

/// Compute the flowed, attempted-duration-adjusted FPS for a Flutter
/// application specified by [uiThread] and [gpuThread].
///
/// "Flowed" means that we only count frames that reach a unique display level
/// VSYNC.  This makes the FPS value we produce more representative of how
/// many frames the end user actually sees.
///
/// "Attempted-duration-adjusted" means that we divide only the amount of time
/// we spent attempting to render, rather than the last frame timestamp minus
/// the first frame timestamp.  This is desirable if for example, we are
/// computing FPS for a scenario that has pockets of time where we
/// legitimately do not need to animate.  This is implemented by dividing by
/// amount of time we spent with a frame request pending or in the full "vsync
/// callback" -> "VSYNC" pipeline.
double _computeFps(Model model, Thread uiThread, Thread gpuThread) {
  final refreshRate = _computeDisplayRefreshRate(model);
  final events = uiThread.events + gpuThread.events;

  final List<_Group> groups = [];
  // An intermediate "curr" group that we use to compute [groups].
  var group = _Group();

  for (final event in events) {
    TimePoint maybeNewEnd;
    if (event is AsyncEvent &&
        event.category == 'flutter' &&
        event.name == 'Frame Request Pending') {
      maybeNewEnd = event.start + event.duration;
    } else if (event is DurationEvent &&
        event.category == 'flutter' &&
        event.name == 'vsync callback') {
      final maybeFollowingVsync = findFollowingVsync(event);
      if (maybeFollowingVsync == null) {
        maybeNewEnd =
            _max(event.start + event.duration, event.start + refreshRate);
      } else {
        maybeNewEnd =
            _max(event.start + event.duration, maybeFollowingVsync.start);
      }
    } else if (event is DurationEvent &&
        event.category == 'flutter' &&
        event.name == 'GPURasterizer::Draw') {
      maybeNewEnd = event.start + event.duration;
    } else {
      continue;
    }

    if (group.events.isEmpty) {
      group.events.add(event);
      group.end = maybeNewEnd;
    } else if (event.start <= group.end) {
      group.events.add(event);
      group.end = _max(group.end, maybeNewEnd);
    } else {
      groups.add(group);
      group = _Group()..end = maybeNewEnd;
    }
  }

  // Flush the final [group] now that we have exhausted [events].
  if (group.events.isNotEmpty) {
    groups.add(group);
  }

  if (groups.isEmpty) {
    throw ArgumentError('Computed 0 frame groups when processing $model');
  }

  final Set<Event> countedDisplayVsyncs = {};
  TimeDelta totalDuration = TimeDelta.zero();

  for (final group in groups) {
    // Hitting this indicates a logic error in the above grouping code.
    assert(group.events.isNotEmpty);

    // We only start counting time at the first "VSYNC", in order to ensure
    // that the pipeline warming up does not penalize FPS throughput.  The
    // part of the duration we're dropping here will be captured by latency
    // benchmarks.
    DurationEvent firstVsync;
    TimePoint lastVsyncStart = TimePoint.fromEpochDelta(TimeDelta.zero());

    final vsyncCallbacks = filterEventsTyped<DurationEvent>(group.events,
        category: 'flutter', name: 'vsync callback');
    final followingVsyncs =
        vsyncCallbacks.map(findFollowingVsync).where((e) => e != null);
    countedDisplayVsyncs.addAll(followingVsyncs);

    firstVsync = followingVsyncs.first;
    if (followingVsyncs.last.start != null) {
      lastVsyncStart = followingVsyncs.last.start;
    }

    // Since we started the window at the beginning of the first "VSYNC", we
    // need to end the window at the last "VSYNC" + refresh rate in order to
    // compute a correct FPS value.
    final adjustedGroupEnd = _max(group.end, lastVsyncStart + refreshRate);
    if (firstVsync == null) {
      totalDuration += adjustedGroupEnd - group.events.first.start;
      continue;
    }
    totalDuration += adjustedGroupEnd - firstVsync.start;
  }

  return countedDisplayVsyncs.length / totalDuration.toSecondsF();
}

class _Results {
  double averageFps;
  List<double> frameBuildTimes;
  List<double> frameRasterizerTimes;
}

_Results _flutterFrameStats(Model model, Thread uiThread, Thread gpuThread) {
  double getDurationInMilliseconds(DurationEvent durationEvent) {
    return durationEvent.duration.toMillisecondsF();
  }

  return _Results()
    ..averageFps = _computeFps(model, uiThread, gpuThread)
    ..frameBuildTimes = filterEventsTyped<DurationEvent>(uiThread.events,
            category: 'flutter', name: 'vsync callback')
        .map(getDurationInMilliseconds)
        .toList()
    ..frameRasterizerTimes = filterEventsTyped<DurationEvent>(gpuThread.events,
            category: 'flutter', name: 'GPURasterizer::Draw')
        .map(getDurationInMilliseconds)
        .toList();
}

List<TestCaseResults> flutterFrameStatsMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  if (metricsSpec.name != 'flutter_frame_stats') {
    throw ArgumentError(
        'Error, unexpected metrics name "${metricsSpec.name}" in '
        'flutterFrameStatsMetricsProcessor');
  }

  final extraArgs = metricsSpec.extraArgs;
  if (!(extraArgs.containsKey('flutterAppName') &&
      extraArgs['flutterAppName'] is String)) {
    throw ArgumentError(
        'Error, expected metrics spec extra args $extraArgs to contain String '
        'field "flutterAppName"');
  }
  final String flutterAppName = extraArgs['flutterAppName'];

  // TODO(PT-212): Should only iterate on flutter processes.
  // final flutterProcesses = model.processes
  //     .where((process) => process.name.startsWith('io.flutter.'));
  final flutterProcesses = model.processes;

  for (final process in flutterProcesses) {
    final uiThreads = process.threads
        .where((thread) =>
            thread.name.startsWith(flutterAppName) &&
            thread.name.endsWith('.ui'))
        .toList();
    final gpuThreads = process.threads
        .where((thread) =>
            thread.name.startsWith(flutterAppName) &&
            thread.name.endsWith('.gpu'))
        .toList();

    if (uiThreads.length != gpuThreads.length) {
      throw ArgumentError('Found unequal number of UI and GPU Flutter threads');
    }
    if (uiThreads.isEmpty && gpuThreads.isEmpty) {
      continue;
    }

    // TODO: We are assuming that threads are in order, instead we should verify
    // that they have the same name % suffix
    final results =
        _flutterFrameStats(model, uiThreads.first, gpuThreads.first);
    return [
      TestCaseResults(
          '${flutterAppName}_fps', Unit.framesPerSecond, [results.averageFps]),
      TestCaseResults(
          'frame_build_times', Unit.milliseconds, results.frameBuildTimes),
      TestCaseResults('frame_rasterizer_times', Unit.milliseconds,
          results.frameRasterizerTimes),
    ];
  }

  throw ArgumentError(
      'Failed to find any matching flutter process in $model for flutter app '
      'name $flutterAppName');
}
