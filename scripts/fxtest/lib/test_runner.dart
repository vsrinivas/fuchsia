// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

class TestRunner {
  static final TestRunner runner = TestRunner();

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
    String workingDirectory,
    Function(String) realtimeOutputSink,
    Function(String) realtimeErrorSink,
  }) async {
    // When no extra listeners are supplied for this process, we can
    // run it very simply.
    if (realtimeOutputSink == null && realtimeErrorSink == null) {
      return Process.run(command, args, workingDirectory: workingDirectory);
    }

    Process process = await Process.start(
      command,
      args,
      workingDirectory: workingDirectory,
    );

    var outputBroadcast = process.stdout
        .asBroadcastStream()
        .transform(utf8.decoder)
        .transform(LineSplitter());
    var errorBroadcast = process.stderr
        .asBroadcastStream()
        .transform(utf8.decoder)
        .transform(LineSplitter());

    var _stdOut = StringBuffer();
    // Register listeners and hold onto StreamSubscriptions to later cancel.
    var outputListeners = <StreamSubscription>[
      outputBroadcast.listen((String val) {
        _stdOut.write(val);
      }),
      outputBroadcast.listen(realtimeOutputSink),
    ];

    var _stdErr = StringBuffer();
    var errorListeners = <StreamSubscription>[
      errorBroadcast.listen((String val) {
        _stdErr.write(val);
      }),
      errorBroadcast.listen(realtimeErrorSink),
    ];

    // Wait for command to actually end.
    int _exitCode = await process.exitCode;

    // Fire off all cancels without waiting one at a time, then wait
    // in one big batch at the end.
    List<Future> cleanUpFutures = [
      ...outputListeners.map((var listener) => listener.cancel()),
      ...errorListeners.map((var listener) => listener.cancel())
    ];
    await Future.wait(cleanUpFutures);

    // Return the same thing as if we'd used `Process.run`.
    return ProcessResult(
      process.pid,
      _exitCode,
      _stdOut.toString(),
      _stdErr.toString(),
    );
  }
}
