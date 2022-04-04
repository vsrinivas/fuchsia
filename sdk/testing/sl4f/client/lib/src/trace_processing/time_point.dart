// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'time_delta.dart';

/// A [TimePoint] represents a point in time represented as an integer number
/// of nanoseconds elapsed since an arbitrary point in the past.
class TimePoint implements Comparable<TimePoint> {
  int _ticks = 0;

  TimePoint();

  TimePoint.fromEpochDelta(TimeDelta ticks)
      : this._timePoint(ticks.toNanoseconds());

  TimeDelta toEpochDelta() => TimeDelta.fromNanoseconds(_ticks);

  TimePoint operator +(TimeDelta timeDelta) {
    return TimePoint._timePoint(_ticks + timeDelta.toNanoseconds());
  }

  TimeDelta operator -(TimePoint other) =>
      TimeDelta.fromNanoseconds(_ticks - other._ticks);

  bool operator <(TimePoint other) => _ticks < other._ticks;
  bool operator >(TimePoint other) => _ticks > other._ticks;
  bool operator <=(TimePoint other) => _ticks <= other._ticks;
  bool operator >=(TimePoint other) => _ticks >= other._ticks;

  @override
  bool operator ==(dynamic other) =>
      other is TimePoint && _ticks == other._ticks;
  @override
  int get hashCode => _ticks.hashCode;
  @override
  int compareTo(TimePoint other) => _ticks.compareTo(other._ticks);

  TimePoint._timePoint(this._ticks);
}
