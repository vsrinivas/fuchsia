// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:meta/meta.dart';

class ProcessArgs {
  final String command;
  final List<String> args;
  ProcessArgs(this.command, this.args);

  @override
  String toString() => '$command ${args.join(' ')}';
}

class TestRunner {
  /// Wrapper which runs commands and not only collects all output for a
  /// [ProcessResult], but also optionally emits realtime events from nested
  /// stdout.
  ///
  /// The [run] method takes an optional [realtimeOutputSink], which is used to
  /// flush output from the process running individual tests. To achieve this,
  /// two intermediate streams are created, one each for the sub-process's
  /// [stdout] and [stderr], with listeners that maintain complete copies for
  /// our value and which dump each update into [realtimeOutputSink] if it was
  /// supplied.
  Future<ProcessResult> run(
    String command,
    List<String> args, {
    @required String workingDirectory,
    Function(String) realtimeOutputSink,
    Function(String) realtimeErrorSink,
  }) async {
    var processArgs = _buildProcessArgs(command, args);
    Process process = await Process.start(
      processArgs.command,
      processArgs.args,
      workingDirectory: workingDirectory,
    );

    var _stdOut = StringBuffer();
    process.stdout.transform(utf8.decoder).transform(LineSplitter()).listen(
      (String val) {
        _stdOut.writeln(val);
        if (realtimeOutputSink != null) {
          realtimeOutputSink(val);
        }
      },
    );

    var _stdErr = StringBuffer();
    process.stderr.transform(utf8.decoder).transform(LineSplitter()).listen(
      (String val) {
        _stdErr.writeln(val);
        if (realtimeErrorSink != null) {
          realtimeErrorSink(val);
        }
      },
    );

    // Wait for test to actually end.
    int _exitCode = await process.exitCode;

    // Return the same thing as if we'd used `Process.run`.
    return ProcessResult(
      process.pid,
      _exitCode,
      realtimeOutputSink == null ? _stdOut.toString() : '',
      _stdErr.toString(),
    );
  }

  ProcessArgs _buildProcessArgs(String command, List<String> args) {
    return ProcessArgs(command, args);
  }
}

class SymbolizingTestRunner extends TestRunner {
  final String fx;

  SymbolizingTestRunner({
    @required this.fx,
  }) : assert(fx != null && fx != '');

  @override
  ProcessArgs _buildProcessArgs(String command, List<String> args) {
    return ProcessArgs('bash', [
      // `-o pipefail` forwards the exitcode from `command` (the test itself)
      // instead of the always-zero exitcode from symbolize
      '-o',
      'pipefail',
      // `-c` keeps the pipe in the domain of `bash`, and not in the domain of
      // `fx shell ...`, which leads to "can't find fx" errors (since that's)
      // not on the device
      '-c',
      [command, ...args, '|', fx, 'symbolize'].join(' ')
    ]);
  }
}
