// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Library for gracefully handling errors and uncaught exceptions
/// and presenting them in an intuitive way to the user.
library crash_handling;

import 'io.dart';

/// An exception that stops the program from running, but is caused by
/// a known/expected failure (e.g. invalid options, wrong build).
/// We would print the exception message differently.
class KnownFailure implements Exception {
  final Object underlying;
  KnownFailure(this.underlying);

  @override
  String toString() {
    return '${underlying.toString()}';
  }
}

Future<String> saveCrashDiagnostics(
    Io io, dynamic errorOrException, StackTrace stackTrace) async {
  final file = io.createTempFile('crash_report.txt');
  final sink = file.openWrite();

  final rawStatusOutput = await io.fx.getSubCommandOutput('status');

  try {
    sink
      ..writeln(errorOrException)
      ..writeln('')
      ..writeln('======== fx status ========')
      ..writeln(rawStatusOutput)
      ..writeln('')
      ..writeln('======== stack trace ========')
      ..writeln(stackTrace);
  } finally {
    await sink.close();
  }

  return file.path;
}

Future<void> withExceptionHandler(Future<void> Function() body) async {
  try {
    await body();
  } on KnownFailure catch (e) {
    // On known failures, simply print the failure and exit.
    final io = Io.get();
    io.err.writeln(e);
    io.exitCode = 2;

    // ignore: avoid_catches_without_on_clauses
  } catch (e, stackTrace) {
    // Generic top-level exception handler
    final io = Io.get();
    String diagnosticsPath = await saveCrashDiagnostics(io, e, stackTrace);

    io.err.writeln('''
════════════════════════════════════════════════════════════
Oops, `fx codesize` has crashed.

Brief error description:
$e

Please file a Monorail under component `Tools>codesize`,
and attach the file containing diagnostics at:
$diagnosticsPath
════════════════════════════════════════════════════════════
'''
        .trim());
    io.exitCode = 1;
  }
}
