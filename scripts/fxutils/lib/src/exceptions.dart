// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class FailedProcessException implements Exception {
  final List<String> command;
  final int exitCode;
  final String? stdout;
  final String? stderr;
  FailedProcessException({
    required this.command,
    required this.exitCode,
    required this.stdout,
    required this.stderr,
  });

  @override
  String toString() =>
      '<FailedProcessException ${command.join(' ')}::$exitCode "$stderr" />';
}
