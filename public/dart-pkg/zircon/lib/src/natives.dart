// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

// ignore_for_file: native_function_body_in_non_sdk_code

class MxTime {
  static int _get(int clockId) native "MxTime_Get";

  // Clock ID zero is MX_CLOCK_MONOTONIC.
  static int timerMillisecondClock() => _get(0) ~/ (1000 * 1000);
}

