// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A [TimeDelta] represents the difference between two [TimePoint]s.
///
/// We use this instead of the standard Dart [Duration] class because
/// [Duration] only supports microseconds, and we want to process trace data
/// with timestamps that have nanosecond granularity.
class TimeDelta implements Comparable<TimeDelta> {
  int _delta = 0;

  TimeDelta();

  TimeDelta.zero() : this._timeDelta(0);

  TimeDelta.fromNanoseconds(num nanoseconds)
      : this._timeDelta(nanoseconds.toInt());
  TimeDelta.fromMicroseconds(num microseconds)
      : this._timeDelta((1000 * microseconds).toInt());
  TimeDelta.fromMilliseconds(num milliseconds)
      : this._timeDelta((1000 * 1000 * milliseconds).toInt());
  TimeDelta.fromSeconds(num seconds)
      : this._timeDelta((1000 * 1000 * 1000 * seconds).toInt());

  int toNanoseconds() => _delta;

  int toMicroseconds() => toNanoseconds() ~/ 1000;

  int toMilliseconds() => toMicroseconds() ~/ 1000;

  int toSeconds() => toMilliseconds() ~/ 1000;

  double toNanosecondsF() => _delta.toDouble();

  double toMicrosecondsF() => _delta / 1000.0;

  double toMillisecondsF() => _delta / (1000.0 * 1000.0);

  double toSecondsF() => _delta / (1000.0 * 1000.0 * 1000.0);

  TimeDelta operator +(TimeDelta other) =>
      TimeDelta._timeDelta(_delta + other._delta);

  TimeDelta operator -(TimeDelta other) =>
      TimeDelta._timeDelta(_delta - other._delta);

  TimeDelta operator *(double factor) =>
      TimeDelta._timeDelta((_delta * factor).round());

  bool operator <(TimeDelta other) => _delta < other._delta;
  bool operator >(TimeDelta other) => _delta > other._delta;
  bool operator <=(TimeDelta other) => _delta <= other._delta;
  bool operator >=(TimeDelta other) => _delta >= other._delta;

  @override
  bool operator ==(dynamic other) =>
      other is TimeDelta && _delta == other._delta;
  @override
  int get hashCode => _delta.hashCode;
  @override
  int compareTo(TimeDelta other) => _delta.compareTo(other._delta);

  TimeDelta._timeDelta(this._delta);
}
