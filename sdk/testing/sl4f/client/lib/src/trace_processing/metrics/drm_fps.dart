// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../metrics_results.dart';
import '../metrics_spec.dart';
import '../time_delta.dart';
import '../trace_model.dart';
import 'common.dart';

// This file implements the DRM FPS metric.  Note that DRM here does not refer
// to digital rights management, but instead Direct Rendering Manager (https://en.wikipedia.org/wiki/Direct_Rendering_Manager).
// While Direct Rendering Manager is something specific to Linux, Fuchsia
// implements an equivalent of the metric for the purpose of comparison to
// existing platforms that use the DRM FPS metric.

/// Iterate over adjacent pairs of elements in [_iterable], by applying [_f]
/// to each pair.
///
/// In other words, perform [x0, x1, x2, ...] -> [f(x0, x1), f(x1, x2), ...].
/// If [_iterable] is of length 0 or 1, then the [_AdjacentPairIterable] will
/// iterate over 0 elements.
class _AdjacentPairIterable<T, U> extends Iterable<U> {
  final Iterable<T> _iterable;
  final U Function(T, T) _f;

  _AdjacentPairIterable(this._iterable, this._f);

  @override
  Iterator<U> get iterator =>
      _AdjacentPairIterator<T, U>(_iterable.iterator, _f);
}

class _AdjacentPairIterator<T, U> extends Iterator<U> {
  final Iterator<T> _iterator;
  final U Function(T, T) _f;

  U _current;
  T _previous;

  _AdjacentPairIterator(this._iterator, this._f) {
    if (!_iterator.moveNext()) {
      return;
    }
    _previous = _iterator.current;
  }

  @override
  bool moveNext() {
    if (!_iterator.moveNext()) {
      return false;
    }
    _current = _f(_previous, _iterator.current);
    _previous = _iterator.current;
    return true;
  }

  @override
  U get current => _current;
}

List<TestCaseResults> drmFpsMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  if (metricsSpec.name != 'drm_fps') {
    throw ArgumentError(
        'Error, unexpected metrics name "${metricsSpec.name}" in '
        'drmFpsMetricsProcessor');
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

    if (uiThreads.isEmpty) {
      continue;
    }

    final uiThread = uiThreads.first;

    final vsyncs = filterEventsTyped<DurationEvent>(uiThread.events,
            category: 'flutter', name: 'vsync callback')
        .map(findFollowingVsync)
        .where((e) => e != null)
        .toSet()
        .toList()
          ..sort((a, b) => a.start.compareTo(b.start));
    final drmFpsValues =
        _AdjacentPairIterable(vsyncs, (a, b) => (b.start - a.start))
            .where((x) => x < TimeDelta.fromSeconds(1))
            .map((x) => 1.0 / x.toSecondsF())
            .toList();

    // In a better world, we would not need to separately export percentiles
    // of the list of DRM FPS values.  Unfortunately though, our performance
    // metrics dashboard is hard-coded to compute precisely the
    //     * count
    //     * maximum
    //     * mean of logs (i.e. mean([log(x) for x in xs]))
    //     * mean
    //     * min
    //     * sum
    //     * variance
    // of lists of values.  So instead, we compute percentiles and export them
    // as their own metrics, which contain lists of size 1.  Unfortunately this
    // also means that useless statistics for the percentile metric will be
    // generated.
    //
    // If we ever switch to a performance metrics dashboard that supports
    // specifying what statistics to compute for a metric, then we should remove
    // these separate percentile metrics.
    final p10 = computePercentile(drmFpsValues, 10);
    final p50 = computePercentile(drmFpsValues, 50);
    final p90 = computePercentile(drmFpsValues, 90);

    return [
      TestCaseResults('drm_fps', Unit.framesPerSecond, drmFpsValues),
      TestCaseResults('drm_fps_p10', Unit.framesPerSecond, [p10]),
      TestCaseResults('drm_fps_p50', Unit.framesPerSecond, [p50]),
      TestCaseResults('drm_fps_p90', Unit.framesPerSecond, [p90]),
    ];
  }

  throw ArgumentError(
      'Failed to find any matching flutter process in $model for flutter app '
      'name $flutterAppName');
}
