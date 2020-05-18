import 'dart:io';

import 'package:fxtest/start_process.dart';

/// Creates a [StartProcess] that always returns the provided [process].
StartProcess mockStartProcess(Process process) =>
    (String executable, List<String> arguments,
            {String workingDirectory,
            Map<String, String> environment,
            bool includeParentEnvironment,
            bool runInShell,
            ProcessStartMode mode}) =>
        Future.value(process);

/// A mock process. By default, the process exits immediately and immediately
/// completes its stdout and stderr streams.
class MockProcess implements Process {
  @override
  final Stream<List<int>> stdout;
  @override
  final Stream<List<int>> stderr;
  @override
  final Future<int> exitCode;

  MockProcess({stdout, stderr, Future<int> exitCode})
      : stdout = stdout ?? Stream.empty(),
        stderr = stderr ?? Stream.empty(),
        exitCode = exitCode ?? Future<int>.value(0);

  @override
  bool kill([ProcessSignal signal = ProcessSignal.sigterm]) {
    throw UnimplementedError();
  }

  @override
  int get pid => 1000; // Arbitrary valid pid.

  @override
  IOSink get stdin => throw UnimplementedError();
}
