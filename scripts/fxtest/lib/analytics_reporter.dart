// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:fxutils/fxutils.dart';
import 'package:meta/meta.dart';

const String analyticsScript = 'tools/devshell/lib/metrics_custom_report.sh';

/// Connection for `fx test` to log metrics to Google Analytics.
class AnalyticsReporter {
  /// Object that knows the location of all relevant Fuchsia artifacts. Used
  /// to turn relative paths into absolute paths.
  final IFxEnv fxEnv;

  /// Shell script able to send metrics to Google Analytics.
  ///
  /// Likely `tools/devshell/lib/metrics_custom_report.sh`
  final String reporterCmd;
  AnalyticsReporter({
    @required this.fxEnv,
    this.reporterCmd = analyticsScript,
  });

  /// No-op constructor useful for tests or when certain flags are passed,
  /// (e.g., `--dryrun`).
  AnalyticsReporter.noop()
      : reporterCmd = null,
        fxEnv = null;

  Future<void> report({
    @required String subcommand,
    @required String action,
    String label,
  }) async {
    if (reporterCmd == null) {
      return;
    }

    List<String> args = [
      '${fxEnv.fuchsiaDir}/$reporterCmd',
      subcommand,
      action,
    ];
    if (label != null && label != '') {
      args.add(label);
    }
    ProcessResult result = await Process.run('bash', args);
    if (result.exitCode != 0) {
      print(
        'Error ${result.exitCode} when running `bash ${args.join(' ')}`:'
        '${result.stderr}',
      );
    }
  }
}
