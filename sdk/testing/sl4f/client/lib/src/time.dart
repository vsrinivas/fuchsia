// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:core';

import 'sl4f_client.dart';

/// Check the time as reported on the DUT using [Sl4f].
class Time {
  final Sl4f _sl4f;

  /// Constructs a [Time] object.
  Time(this._sl4f);

  /// Returns the system time on the DUT in UTC.
  Future<DateTime> systemTime() async {
    final timestampMillis = await _sl4f.request('time_facade.SystemTimeMillis');
    return DateTime.fromMillisecondsSinceEpoch(timestampMillis, isUtc: true);
  }

  /// Returns whether or not system time on the DUT is synchronized to an external source.
  Future<bool> isSystemTimeSynchronized() async =>
      await _sl4f.request('time_facade.IsSynchronized');
}
