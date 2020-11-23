// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'package:fxutils/fxutils.dart';

/// Re-entry helper for running other fx subcommands from within any subcommand
/// written in Dart.
///
/// [Fx] instances are source-tree aware (via [RootLocator]), and launch
/// processes via a helper [startProcess].
///
/// Usage:
/// ```dart
/// final fx = Fx();
///
/// // Run prepared commands
/// final deviceName = await fx.getDeviceName();
///
/// // Run custom, specific commands.
/// // This runs `fx some-command`.
/// final output = await fx.getSubCommandOutput('some-command');
/// ```
class Fx {
  final FxEnv? fxEnv;
  final ProcessLauncher _processLauncher;
  Fx({
    required this.fxEnv,
    StartProcess? processStarter,
  }) : _processLauncher = ProcessLauncher(processStarter: processStarter);

  factory Fx.mock(Process process, [FxEnv? fxEnv]) => Fx(
        processStarter: returnGivenProcess(process),
        fxEnv: fxEnv,
      );

  Future<String?> getDeviceName() async {
    return await getSubCommandOutput('get-device');
  }

  Future<bool> isPackageServerRunning() async {
    try {
      await getSubCommandOutput('is-package-server-running');
    } on FailedProcessException {
      return false;
    }
    return true;
  }

  Future<bool> updateIfInBase(
    Iterable<String> testNames, {
    int batchSize = 50,
  }) async {
    final testNamesIterator = ListIterator<String>.from(testNames.toList());
    while (testNamesIterator.isNotEmpty) {
      final result = await runFxSubCommand(
        'update-if-in-base',
        args: testNamesIterator.take(batchSize),
      );
      if (result.exitCode != 0) {
        return false;
      }
    }
    return true;
  }

  /// Helper which invokes a command and returns the response.
  ///
  /// Intentionally offers no realtime access to output, or to the stderr. This
  /// is meant to simplify the task of getting output from an fx subcommand.
  Future<String?> getSubCommandOutput(
    /// fx subcommand to execute. For example, "device-name", "status", etc.
    String cmd, {

    /// Optional list of arguments to pass to the subcommand.
    List<String>? args,

    /// Accepted exit codes. Unexpected exit codes will throw a
    /// [FailedProcessException]. Set this to [null] to never throw an
    /// exception.
    List<int> allowedExitCodes = const [0],

    /// Convenience flag to remove trailing whitespace from the command, since
    /// that is unlikely to be desired.
    bool shouldTrimTrailing = true,
  }) async {
    final processResult = await runFxSubCommand(
      cmd,
      args: args,
    );
    if (!allowedExitCodes.contains(processResult.exitCode)) {
      final fullCommand = [fxEnv!.fx, cmd, ...?args];
      throw FailedProcessException(
        command: fullCommand,
        exitCode: processResult.exitCode,
        stdout: processResult.stdout,
        stderr: processResult.stderr,
      );
    }
    return shouldTrimTrailing
        // ignore: avoid_as
        ? (processResult.stdout as String).trimRight()
        : processResult.stdout;
  }

  /// Basic wrapper around running an fx subcommand which exposes all necessary
  /// controls to the caller.
  Future<ProcessResult> runFxSubCommand(
    /// Specific `fx` subcommand to execute.
    String cmd, {

    /// Argument and flags for the subcommand. Optional.
    List<String>? args,

    /// I/O mode of the spawned process. Defaults to
    /// [ProcessStartMode.inheritStdio] if unset and if [stdoutSink] and
    /// [stderrSink] are null.
    ProcessStartMode mode = ProcessStartMode.normal,

    // /// Handler for converting raw bytes into readable strings
    SystemEncoding encoding = systemEncoding,
  }) async {
    return _processLauncher.run(
      fxEnv!.fx,
      [cmd, ...?args],
      mode: mode,
      outputEncoding: encoding,
    );
  }
}
