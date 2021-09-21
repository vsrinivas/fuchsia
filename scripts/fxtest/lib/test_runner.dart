// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:meta/meta.dart';

/// Disposable runner which executes a single test and presents its output in
/// realtime (via streams) and aggregates for the end (via a [StringBuffer]).
class TestRunner {
  final StreamController<String> _stdoutController;
  final StartProcess _startProcess;

  TestRunner({StartProcess startProcess})
      : _stdoutController = StreamController<String>(),
        _startProcess = startProcess ?? Process.start;

  Stream<String> get output => _stdoutController.stream;
  void addOutput(String content) => _stdoutController.sink.add(content);

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
    Map<String, String> environment,
  }) async {
    var processArgs = _buildProcessArgs(command, args);
    Process process = await _startProcess(
      processArgs.command,
      processArgs.args,
      environment: environment,
      workingDirectory: workingDirectory,
    );

    var utf8decoder = Utf8Decoder(allowMalformed: true);
    var _stdOut = StringBuffer();
    var stdOutFinish = Future(() async {
      var lines =
          process.stdout.transform(utf8decoder).transform(LineSplitter());
      await for (var val in lines) {
        _stdOut.writeln(val);
        addOutput(val);
      }
    });

    var _stdErr = StringBuffer();
    var stdErrFinish = Future(() async {
      var lines =
          process.stderr.transform(utf8decoder).transform(LineSplitter());
      await for (var val in lines) {
        _stdErr.writeln(val);
        addOutput(val);
      }
    });

    // Wait for test to actually end. This means waiting for the test process
    // to terminate and for the output streams to be flushed.
    var results =
        await Future.wait([process.exitCode, stdOutFinish, stdErrFinish]);
    var _exitCode = results[0];

    await close();

    // Return the same thing as if we'd used `Process.run`.
    return ProcessResult(
      process.pid,
      _exitCode,
      _stdOut.toString(),
      _stdErr.toString(),
    );
  }

  Future close() {
    return _stdoutController.close();
  }

  _ProcessArgs _buildProcessArgs(String command, List<String> args) {
    return _ProcessArgs(command, args);
  }
}

class SymbolizingTestRunner extends TestRunner {
  final String fx;

  SymbolizingTestRunner({
    @required this.fx,
  }) : assert(fx != null && fx != '');

  factory SymbolizingTestRunner.builder(TestsConfig testsConfig) =>
      SymbolizingTestRunner(fx: testsConfig.fxEnv.fx);

  @override
  _ProcessArgs _buildProcessArgs(String command, List<String> args) {
    return _ProcessArgs('bash', [
      // `-o pipefail` forwards the exitcode from `command` (the test itself)
      // instead of the always-zero exitcode from symbolize
      '-o',
      'pipefail',
      // `-c` keeps the pipe in the domain of `bash`, and not in the domain of
      // `fx shell ...`, which leads to "can't find fx" errors (since that's
      // not on the device)
      '-c',
      [command, ...args, '|', fx, 'symbolize'].join(' ')
    ]);
  }
}

class _ProcessArgs {
  final String command;
  final List<String> args;
  _ProcessArgs(this.command, this.args);

  @override
  String toString() => '$command ${args.join(' ')}';
}
