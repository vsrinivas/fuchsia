// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' as convert;
import 'dart:io';

import 'package:fxutils/fxutils.dart';

class ProcessLauncher {
  final StartProcess processStarter;
  ProcessLauncher({StartProcess? processStarter})
      : processStarter = processStarter ??
            Process.start as Future<Process> Function(String, List<String>,
                {Map<String, String>? environment,
                bool? includeParentEnvironment,
                ProcessStartMode? mode,
                bool? runInShell,
                String? workingDirectory});

  Future<Process> start(String executable, List<String> arguments,
          {String? workingDirectory,
          Map<String, String> environment = const {},
          bool includeParentEnvironment = true,
          bool runInShell = false,
          ProcessStartMode mode = ProcessStartMode.normal}) =>
      processStarter(
        executable,
        arguments,
        workingDirectory: workingDirectory,
        environment: environment,
        includeParentEnvironment: includeParentEnvironment,
        runInShell: runInShell,
        mode: mode,
      );

  Future<ProcessResult> run(
    String executable,
    List<String> arguments, {
    String? workingDirectory,
    Map<String, String> environment = const {},
    bool includeParentEnvironment = true,
    bool runInShell = false,
    ProcessStartMode mode = ProcessStartMode.normal,
    convert.Encoding? outputEncoding = systemEncoding,
  }) async {
    final _stdout = <int>[];
    final _stderr = <int>[];
    final process = await start(
      executable,
      arguments,
      workingDirectory: workingDirectory,
      environment: environment,
      includeParentEnvironment: includeParentEnvironment,
      runInShell: runInShell,
      mode: mode,
    );

    final stdOutFinish = Future(() async {
      await process.stdout.forEach(_stdout.addAll);
    });
    final stdErrFinish = Future(() async {
      await process.stderr.forEach(_stderr.addAll);
    });

    final results = await Future.wait(
      [
        process.exitCode,
        stdOutFinish.then((_) => /* placeholder */ 0),
        stdErrFinish.then((_) => /* placeholder */ 0)
      ],
    );

    return ProcessResult(
      process.pid,
      results[0],
      outputEncoding != null ? outputEncoding.decode(_stdout) : _stdout,
      outputEncoding != null ? outputEncoding.decode(_stderr) : _stderr,
    );
  }
}

/// Creates a [StartProcess] that always returns the provided [process].
StartProcess returnGivenProcess(Process process) => (
      String executable,
      List<String> arguments, {
      String? workingDirectory,
      Map<String, String>? environment,
      bool? includeParentEnvironment,
      bool? runInShell,
      ProcessStartMode? mode,
    }) =>
        Future.value(process);
