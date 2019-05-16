// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File;

import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

import 'dump.dart';
import 'sl4f_client.dart';

String _traceNameToTargetPath(String traceName) {
  return '/tmp/$traceName-trace.json';
}

final _log = Logger('traceutil');

class Traceutil {
  final Sl4f _sl4f;
  final Dump _dump;

  /// Constructs a [Traceutil] object.
  ///
  /// It can optionally take an [Sl4f] object, if not passed, one will be
  /// created using the environment variables to connect to the device.
  Traceutil([Sl4f sl4f, Dump dump])
      : _sl4f = sl4f ?? Sl4f.fromEnvironment(),
        _dump = dump ?? Dump();

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client is closed instead.
  void close() {
    _sl4f.close();
  }

  /// Starts tracing for the given [duration].
  ///
  /// The trace output will be saved to a path implied by [traceName], and can
  /// be retrieved later via [downloadTraceFile].
  Future<bool> trace(
      {@required Duration duration,
      @required String traceName,
      String categories,
      int bufferSize}) {
    // Invoke `/bin/trace record --duration=$duration --categories=$categories
    // --output-file=$outputFile --buffer-size=$bufferSize` on the target
    // device via ssh.
    final outputFile = _traceNameToTargetPath(traceName);
    final durationSeconds = duration.inSeconds;
    String command = 'trace record --duration=$durationSeconds';
    if (categories != null) {
      command += ' --categories=$categories';
    }
    if (outputFile != null) {
      command += ' --output-file=$outputFile';
    }
    if (bufferSize != null) {
      command += ' --buffer-size=$bufferSize';
    }
    return _sl4f.ssh(command);
  }

  /// Copies the trace file specified by [traceName] off of the target device,
  /// and then saves it to the dump directory.
  ///
  /// A [trace] call with the same [traceName] must have successfully
  /// completed before calling [downloadTraceFile].
  ///
  /// Returns the download trace [File].
  Future<File> downloadTraceFile(String traceName) async {
    _log.info('Traceutil: Downloading trace $traceName');
    final tracePath = _traceNameToTargetPath(traceName);
    final String response = await _sl4f
        .request('traceutil_facade.GetTraceFile', {'path': tracePath});
    return _dump.writeAsBytes(
        '$traceName-trace', 'json', utf8.encode(response));
  }
}
