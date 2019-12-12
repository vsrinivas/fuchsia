// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../metrics_results.dart';
import '../metrics_spec.dart';
import '../trace_model.dart';
import 'common.dart';

/// Iterate over a sequence of [T1]s and [T2]s by applying [_f] to pairs of
/// elements in the sequences.
///
/// In other words, iterate over [x1, x2, ...] and [y1, y2, ...] like
/// [_f(x1, y1), _f(x2, y2), ...].  Iteration stops when any [Iterable] is
/// exhausted.
class _Zip2Iterable<T1, T2, R> extends Iterable<R> {
  final Iterable<T1> _t1s;
  final Iterable<T2> _t2s;
  final R Function(T1, T2) _f;

  _Zip2Iterable(this._t1s, this._t2s, this._f);

  @override
  Iterator<R> get iterator =>
      _Zip2Iterator<T1, T2, R>(_t1s.iterator, _t2s.iterator, _f);
}

class _Zip2Iterator<T1, T2, R> extends Iterator<R> {
  final Iterator<T1> _t1s;
  final Iterator<T2> _t2s;
  final R Function(T1, T2) _f;

  R _current;

  _Zip2Iterator(this._t1s, this._t2s, this._f);

  @override
  bool moveNext() {
    if (!(_t1s.moveNext() && _t2s.moveNext())) {
      return false;
    }
    _current = _f(_t1s.current, _t2s.current);
    return true;
  }

  @override
  R get current => _current;
}

List<TestCaseResults> inputLatencyMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  if (metricsSpec.name != 'input_latency') {
    throw ArgumentError(
        'Error, unexpected metrics name "${metricsSpec.name}" in '
        'inputLatencyMetricsProcessor');
  }

  final inputEvents = filterEventsTyped<DurationEvent>(getAllEvents(model),
      category: 'input', name: 'presentation_on_event');
  final vsyncEvents = inputEvents.map(findFollowingVsync);

  final latencyValues = _Zip2Iterable<DurationEvent, DurationEvent, double>(
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
