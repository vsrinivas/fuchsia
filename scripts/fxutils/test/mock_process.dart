// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

/// A mock process. By default, the process exits immediately and immediately
/// completes its stdout and stderr streams.
class MockProcess implements Process {
  @override
  final Stream<List<int>> stdout;
  @override
  final Stream<List<int>> stderr;
  @override
  final Future<int> exitCode;

  MockProcess({String stdout, String stderr, int exitCode = 0})
      : assert(exitCode != null),
        stdout = stdout != null
            ? Stream.fromIterable([utf8.encode(stdout)])
            : Stream.empty(),
        stderr = stderr != null
            ? Stream.fromIterable([utf8.encode(stderr)])
            : Stream.empty(),
        exitCode = Future<int>.value(exitCode);

  @override
  bool kill([ProcessSignal signal = ProcessSignal.sigterm]) {
    throw UnimplementedError();
  }

  @override
  int get pid => 1000; // Arbitrary valid pid.

  @override
  IOSink get stdin => throw UnimplementedError();
}
