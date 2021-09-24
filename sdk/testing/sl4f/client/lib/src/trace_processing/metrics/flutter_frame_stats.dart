// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

import '../metrics_results.dart';
import '../time_delta.dart';
import '../time_point.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('FlutterFrameStatsMetricsProcessor');

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

class _FpsResult {
  int flowedFrameCount;
  TimeDelta totalDuration;
  TimeDelta frameTimeDiscrepancy;
  double get averageFps => (totalDuration == TimeDelta.zero())
      ? (0.0)
      : (flowedFrameCount / totalDuration.toSecondsF());
}

/// Compute the flowed, attempted-duration-adjusted FPS for a Flutter
/// application specified by [uiThread] and [rasterThread].
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
_FpsResult _computeFps(Model model, Thread uiThread, Thread rasterThread) {
  final refreshRate = _computeDisplayRefreshRate(model);
  final events = uiThread.events + rasterThread.events;

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
        // TODO(fxbug.dev/48263): Only match "VsyncProcessCallback".
        (event.name == 'vsync callback' ||
            event.name == 'VsyncProcessCallback')) {
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
  TimeDelta frameTimeDiscrepancy = TimeDelta.zero();

  for (final group in groups) {
    // Hitting this indicates a logic error in the above grouping code.
    assert(group.events.isNotEmpty);

    // TODO(fxbug.dev/48263): Only match "VsyncProcessCallback".
    final vsyncCallbacks = filterEventsTyped<DurationEvent>(group.events,
            category: 'flutter', name: 'vsync callback')
        .followedBy(filterEventsTyped<DurationEvent>(group.events,
            category: 'flutter', name: 'VsyncProcessCallback'));
    final followingVsyncs =
        vsyncCallbacks.map(findFollowingVsync).where((e) => e != null);

    if (followingVsyncs.isEmpty) {
      print('Warning, found frame group with event count '
          '${group.events.length} and "vsync callback" count '
          '${vsyncCallbacks.length} connected to 0 "VSYNC" events');
      continue;
    }

    countedDisplayVsyncs.addAll(followingVsyncs);

    // We only start counting time at the first "VSYNC", in order to ensure
    // that the pipeline warming up does not penalize FPS throughput.  The
    // part of the duration we're dropping here will be captured by latency
    // benchmarks.
    final firstVsyncStart = followingVsyncs.first.start;
    // Since we started the window at the beginning of the first "VSYNC", we
    // need to end the window at the last "VSYNC" + refresh rate in order to
    // compute a correct FPS value.
    final adjustedGroupEnd =
        _max(group.end, followingVsyncs.last.start + refreshRate);
    totalDuration += adjustedGroupEnd - firstVsyncStart;

    final vsyncTimes = followingVsyncs
        .map((vsync) => (vsync.start - firstVsyncStart).toMillisecondsF())
        .toList();
    final groupDiscrepancy =
        TimeDelta.fromMilliseconds(computeDiscrepancy(vsyncTimes));
    // As discrepancy is defined by a supremum, we report the maximal
    // discrepancy from any one group.
    if (groupDiscrepancy > frameTimeDiscrepancy) {
      frameTimeDiscrepancy = groupDiscrepancy;
    }
  }

  return _FpsResult()
    ..flowedFrameCount = countedDisplayVsyncs.length
    ..totalDuration = totalDuration
    ..frameTimeDiscrepancy = frameTimeDiscrepancy;
}

/// Compute the discrepancy for a series of timestamps.
///
/// Discrepancy is a measure of how far a sequence deviates from uniform, in a
/// way that gives a rough picture of the worst-case jank in the case of
/// framerates.
///
/// It is defined in section 3 of
/// https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/45361.pdf
@visibleForTesting
double computeDiscrepancy(List<double> timestamps) {
  if (timestamps.length < 2) {
    return 0.0;
  }

  timestamps.sort();
  final n = timestamps.length;
  final a = timestamps[0];
  final b = timestamps[n - 1];
  // [a] gets normalized to 1/2N and [b] gets normalized to 1 - 1/2N
  final normalizationFactor = (1.0 - 1.0 / n) / (b - a);
  final normalizationOffset = 1.0 / (2.0 * n) - a * normalizationFactor;

  final normalizedTimestamps = timestamps
      .map((t) => t * normalizationFactor + normalizationOffset)
      .toList();

  // For the normalized series in the interval [0,1], the discrepancy is defined
  // as sup_{c <= d} |\frac{S \cap [c, d]}{N} - (d-c)|. For a chosen interval
  // [c, d], the first term is the actual portion of the timestamps that lie
  // within the interval, while the second term is the expected portion.
  //
  // With a bit of rearrangement, we have \frac{S \cap [c, d]}{N} - (d-c)
  // = \frac{S \cap [0, d]}{N} - d - (\frac{S \cap [0, c]}{N} - c).
  //
  // Thus if we compute the supremum and infimum values for
  // \frac{S \cap [0, x]}{N} - x, we obtain the discrepancy by sending one of c
  // or d to the supremum and the other to the infimum.
  //
  // The supremum will occur when x is equal to one of the timestamps, and the
  // infimum will occur as x approaches one of the timestamps from below, where
  // the value is approaches exactly \frac{1}{N} less than the value when x is
  // actually equal to the timestamp.
  var maximumPrefixDiscrepancy = -1.0;
  var minimumPrefixDiscrepancy = 1.0;
  for (var i = 0; i < n; i++) {
    final prefixDiscrepancy = i.toDouble() / n - normalizedTimestamps[i];
    if (prefixDiscrepancy > maximumPrefixDiscrepancy) {
      maximumPrefixDiscrepancy = prefixDiscrepancy;
    }
    if (prefixDiscrepancy < minimumPrefixDiscrepancy) {
      minimumPrefixDiscrepancy = prefixDiscrepancy;
    }
  }
  final normalizedDiscrepancy =
      maximumPrefixDiscrepancy - minimumPrefixDiscrepancy + 1.0 / n;

  return normalizedDiscrepancy / normalizationFactor;
}

List<double> _computeFrameLatencies(Thread uiThread) {
  return filterEventsTyped<DurationEvent>(uiThread.events,
          category: 'flutter', name: 'vsync callback')
      // TODO(fxbug.dev/48263): Only match "VsyncProcessCallback"
      .followedBy(filterEventsTyped<DurationEvent>(uiThread.events,
          category: 'flutter', name: 'VsyncProcessCallback'))
      .map((event) {
        final followingVsync = findFollowingVsync(event);
        if (followingVsync == null) {
          return null;
        } else {
          return (followingVsync.start - event.start);
        }
      })
      .where((v) => v != null)
      .map((duration) => duration.toMillisecondsF())
      .toList();
}

/// TODO(fxbug.dev/73367): Modify this test to only include VsyncProcessCallback
List<double> _computeRenderFrameTotalDurations(Model model) {
  final modelEvents = getAllEvents(model);
  final startRenderingEvents = filterEventsTyped<DurationEvent>(modelEvents,
          category: 'flutter', name: 'vsync callback')
      .followedBy(filterEventsTyped<DurationEvent>(modelEvents,
          category: 'flutter', name: 'VsyncProcessCallback'));

  final endRenderingEvents =
      startRenderingEvents.map((DurationEvent durationEvent) {
    final followingEvents = filterEventsTyped<DurationEvent>(
        getFollowingEvents(durationEvent),
        category: 'gfx',
        name: 'scenic_impl::Session::ScheduleNextPresent');
    if (followingEvents.isEmpty) {
      return null;
    }
    return followingEvents.first;
  });

  final renderFrameTotalDurations =
      Zip2Iterable<DurationEvent, DurationEvent, double>(
          startRenderingEvents,
          endRenderingEvents,
          (startRenderingEvent, endRenderingEvent) => (endRenderingEvent ==
                  null)
              ? null
              : (endRenderingEvent.start - startRenderingEvent.start)
                  .toMillisecondsF()).where((delta) => delta != null).toList();

  return renderFrameTotalDurations;
}

// We compute the number of frames Flutter produces that are undisplayed, meaning will not be displayed for at least one vsync interval.
// We can do this by measuring how many of Flutter's frames flow into the same Scenic RenderFrame. If N Flutter frames flow into 1 Scenic
// RenderFrame, then we have N-1 undisplayed frames.
int _computeUndisplayedFrameCount(Model model) {
  final startRenderingEvents = filterEventsTyped<DurationEvent>(
      getAllEvents(model),
      category: 'flutter',
      name: 'GPURasterizer::Draw');

  // Make sure the list is de-duped.
  final endRenderingEvents = startRenderingEvents
      .map((DurationEvent durationEvent) {
        final followingEvents = filterEventsTyped<DurationEvent>(
            getFollowingEvents(durationEvent),
            category: 'gfx',
            name: 'RenderFrame');
        if (followingEvents.isEmpty) {
          return null;
        }
        return followingEvents.first;
      })
      .toSet()
      .toList();

  return startRenderingEvents.length - endRenderingEvents.length;
}

class _Results {
  String appName;
  _FpsResult fpsResult;
  List<double> frameBuildTimes;
  List<double> frameRasterizerTimes;
  List<double> frameLatencies;
  List<double> renderFrameTotalDurations;
  int undisplayedFrameCount;
}

String _appResultToString(_Results results) {
  final buffer = StringBuffer()
    ..write('''
===
${results.appName} Flutter Frame Stats
===

''')
    ..write('fps:\n')
    ..write('  ${results.fpsResult.averageFps}\n')
    ..write(
        '    (flow adjusted) frame count: ${results.fpsResult.flowedFrameCount}\n')
    ..write(
        '    (work adjusted) total duration: ${results.fpsResult.totalDuration.toSecondsF()}\n')
    ..write('\n')
    ..write('frame_build_times:\n')
    ..write(describeValues(results.frameBuildTimes, indent: 2))
    ..write('\n')
    ..write('frame_rasterizer_times:\n')
    ..write(describeValues(results.frameRasterizerTimes, indent: 2))
    ..write('\n')
    ..write('frame_latencies:\n')
    ..write(describeValues(results.frameLatencies, indent: 2))
    ..write('\n')
    ..write('render_frame_total_durations:\n')
    ..write(describeValues(results.renderFrameTotalDurations, indent: 2))
    ..write('\n')
    ..write('undisplayed frame count:\n')
    ..write('  ${results.undisplayedFrameCount}\n')
    ..write('\n')
    ..write(
        'frame_time_discrepancy: ${results.fpsResult.frameTimeDiscrepancy.toMillisecondsF()}\n')
    ..write('\n');

  return buffer.toString();
}

/// Compute frame stats metrics for all matching flutter apps in the trace. If
/// [flutterAppName] is specified, only threads whose name starts with
/// [flutterAppName] are considered.
///
/// Returns a list of results with an entry for each flutter app found.
List<_Results> _flutterFrameStats(Model model, {String flutterAppName}) {
  final results = <_Results>[];
  // TODO(fxbug.dev/23073): Should only iterate on flutter processes.
  // final flutterProcesses = model.processes
  //     .where((process) => process.name.startsWith('io.flutter.'));
  final flutterProcesses = model.processes;

  for (final process in flutterProcesses) {
    final uiThreads = process.threads
        .where((thread) =>
            (flutterAppName == null ||
                thread.name.startsWith(flutterAppName)) &&
            thread.name.endsWith('.ui'))
        .toList();
    for (final uiThread in uiThreads) {
      final threadPrefix = uiThread.name.split(RegExp(r'.ui$')).first;
      final appName = flutterAppName ?? threadPrefix;
      // Zircon truncates thread names to 31 characters (32 with the null byte),
      // which needs to be accounted for to process apps with particularly long
      // names.
      //
      // Since the prefix came from a thread name with '.ui' removed from the
      // end, there will be at least three characters available before running
      // into the limit, so we'll be able to match '.gp' or '.ra' in the worst
      // case, which avoids confusion with other flutter threads.
      String gpuThreadName = '$threadPrefix.gpu';
      if (gpuThreadName.length > 31) {
        gpuThreadName = gpuThreadName.substring(0, 31);
      }
      String rasterThreadName = '$threadPrefix.raster';
      if (rasterThreadName.length > 31) {
        rasterThreadName = rasterThreadName.substring(0, 31);
      }
      // Note: Around March 2020, Flutter renamed the GPU thread the Raster
      // thread instead.  In the far out future it might make sense to only
      // accept ".raster" here, but for now accept both names.
      final rasterThreads = process.threads
          .where((thread) =>
              thread.name == gpuThreadName || thread.name == rasterThreadName)
          .toList();
      if (rasterThreads.isEmpty) {
        print('Warning, found ui thread but no raster thread for app $appName');
        continue;
      }
      if (rasterThreads.length > 1) {
        print('Warning, found multiple (${rasterThreads.length}) raster threads'
            ' for app $appName.  Continuing with the first one.');
      }

      final rasterThread = rasterThreads.first;

      double getDurationInMilliseconds(DurationEvent durationEvent) {
        return durationEvent.duration.toMillisecondsF();
      }

      results.add(_Results()
        ..appName = appName
        ..fpsResult = _computeFps(model, uiThread, rasterThread)
        // TODO(fxbug.dev/48263): Only match "VsyncProcessCallback".
        ..frameBuildTimes = filterEventsTyped<DurationEvent>(uiThread.events,
                category: 'flutter', name: 'vsync callback')
            .followedBy(filterEventsTyped<DurationEvent>(uiThread.events,
                category: 'flutter', name: 'VsyncProcessCallback'))
            .map(getDurationInMilliseconds)
            .toList()
        ..frameRasterizerTimes = filterEventsTyped<DurationEvent>(
                rasterThread.events,
                category: 'flutter',
                name: 'GPURasterizer::Draw')
            .map(getDurationInMilliseconds)
            .toList()
        ..frameLatencies = _computeFrameLatencies(uiThread)
        ..renderFrameTotalDurations = _computeRenderFrameTotalDurations(model)
        ..undisplayedFrameCount = _computeUndisplayedFrameCount(model));
    }
  }

  if (results.isEmpty) {
    if (flutterAppName != null) {
      throw ArgumentError(
          'Failed to find any matching flutter process in $model for flutter app '
          'name $flutterAppName');
    } else {
      throw ArgumentError('Failed to find any flutter processes in $model');
    }
  }

  return results;
}

List<TestCaseResults> flutterFrameStatsMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  if (!(extraArgs.containsKey('flutterAppName') &&
      extraArgs['flutterAppName'] is String)) {
    throw ArgumentError(
        'Error, expected metrics spec extra args $extraArgs to contain String '
        'field "flutterAppName"');
  }
  final String flutterAppName = extraArgs['flutterAppName'];

  final results =
      _flutterFrameStats(model, flutterAppName: flutterAppName).first;

  if (results.frameLatencies.isEmpty) {
    throw ArgumentError('Computed 0 frame latency values.');
  }

  _log.info(_appResultToString(results));

  return [
    TestCaseResults('${flutterAppName}_fps', Unit.framesPerSecond,
        [results.fpsResult.averageFps]),
    TestCaseResults('${flutterAppName}_frame_build_times', Unit.milliseconds,
        results.frameBuildTimes),
    TestCaseResults('${flutterAppName}_frame_rasterizer_times',
        Unit.milliseconds, results.frameRasterizerTimes),
    TestCaseResults('${flutterAppName}_frame_latencies', Unit.milliseconds,
        results.frameLatencies),
    TestCaseResults('${flutterAppName}_render_frame_total_durations',
        Unit.milliseconds, results.renderFrameTotalDurations),
    TestCaseResults(
        '${flutterAppName}_frame_time_discrepancy',
        Unit.milliseconds,
        [results.fpsResult.frameTimeDiscrepancy.toMillisecondsF()]),
    TestCaseResults('${flutterAppName}_undisplayed_frame_count', Unit.count,
        [results.undisplayedFrameCount.toDouble()]),
  ];
}

String flutterFrameStatsReport(Model model) {
  final buffer = StringBuffer();

  final results = _flutterFrameStats(model);

  for (final appResult in results) {
    buffer.write(_appResultToString(appResult));
  }

  return buffer.toString();
}
