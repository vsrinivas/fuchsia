// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';
import 'package:fxutils/fxutils.dart';

/// A mock process. By default, the process exits immediately and immediately
/// completes its stdout and stderr streams.
class MockProcess implements Process {
  @override
  final Stream<List<int>> stdout;
  @override
  final Stream<List<int>> stderr;
  @override
  final Future<int> exitCode;

  MockProcess({
    Stream<List<int>>? stdout,
    Stream<List<int>>? stderr,
    Future<int>? exitCode,
  })  : stdout = stdout ?? Stream.empty(),
        stderr = stderr ?? Stream.empty(),
        exitCode = exitCode ?? Future<int>.value(0);

  /// Convenience constructor that deals in raw literals instead of streams
  /// of bytes.
  ///
  /// Because this class mocks processes, and processes deal with streams of
  /// bytes, that is the direct constructor's language. However, since most
  /// process output is ultimately strings, this allows quicker creation of what
  /// developers will often want. And so, while nothing could be more "raw" than
  /// a series of bytes, *streams* of a series of bytes are themselves an
  /// abstraction. Since this constructor accepts raw literals, we name it
  /// thusly.
  factory MockProcess.raw({
    String? stdout,
    String? stderr,
    int exitCode = 0,
  }) =>
      MockProcess(
        exitCode: Future<int>.value(exitCode),
        stdout:
            stdout != null ? Stream.fromIterable([utf8.encode(stdout)]) : null,
        stderr:
            stderr != null ? Stream.fromIterable([utf8.encode(stderr)]) : null,
      );

  @override
  bool kill([ProcessSignal signal = ProcessSignal.sigterm]) {
    throw UnimplementedError();
  }

  @override
  int get pid => 1000; // Arbitrary valid pid.

  @override
  IOSink get stdin => throw UnimplementedError();
}

/// Creates a [StartProcess] that always returns the provided [process].
StartProcess mockStartProcess(Process process) =>
    (String executable, List<String> arguments,
            {String? workingDirectory,
            Map<String, String>? environment,
            bool? includeParentEnvironment,
            bool? runInShell,
            ProcessStartMode? mode}) =>
        Future.value(process);
