// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io' show File, Platform, Process, ProcessResult;

import 'package:collection/collection.dart';
import 'package:logging/logging.dart';

import 'package:path/path.dart' as path;

String _removeSuffix(String string, String suffix) {
  if (!string.endsWith(suffix)) {
    throw ArgumentError('String "$string" does not end with "$suffix"');
  }
  return string.substring(0, string.length - suffix.length);
}

final _log = Logger('Performance');

class PerformancePublish {
  // Names of environment variables used for tagging test results when
  // uploading to the Catapult performance dashboard.

  static const String _catapultDashboardMasterVarName =
      'CATAPULT_DASHBOARD_MASTER';
  static const String _buildbucketIdVarName = 'BUILDBUCKET_ID';
  static const String _buildCreateTimeVarName = 'BUILD_CREATE_TIME';
  static const String _releaseVersion = 'RELEASE_VERSION';
  static const String _catapultDashboardBotVarName = 'CATAPULT_DASHBOARD_BOT';

  PerformancePublish();

  /// Send the given perf test results for upload to the Catapult Dashboard.
  ///
  /// This converts the results to Catapult format.  If uploading to Catapult is
  /// enabled, this puts the resulting file in a directory with a filename
  /// indicating that it should be uploaded.

  Future<void> convertResults(
      String converterPath, File result, Map<String, String> environment,
      {String expectedMetricNamesFile}) async {
    _log.info('Converting the results into the catapult format');

    _checkFuchsiaPerfMetricsNaming(
        result, expectedMetricNamesFile, environment);

    var master = environment[_catapultDashboardMasterVarName];
    var bot = environment[_catapultDashboardBotVarName];
    final buildbucketId = environment[_buildbucketIdVarName];
    final buildCreateTime = environment[_buildCreateTimeVarName];
    var releaseVersion = environment[_releaseVersion];

    bool uploadEnabled = true;
    String logurl;
    int timestamp;
    if (master == null && bot == null) {
      _log.info(
          'convertResults: Infra env vars are not set; treating as a local run.');
      bot = 'local-bot';
      master = 'local-master';
      logurl = 'http://ci.example.com/build/300';
      timestamp = DateTime.now().millisecondsSinceEpoch;
      // Disable uploading so that we don't accidentally upload with the
      // placeholder values set here.
      uploadEnabled = false;
    } else if (master != null &&
        bot != null &&
        buildbucketId != null &&
        buildCreateTime != null) {
      logurl = 'https://ci.chromium.org/b/$buildbucketId';
      timestamp = int.parse(buildCreateTime);
    } else {
      throw ArgumentError(
          'Catapult-related infra env vars are not set consistently');
    }

    final resultsPath = result.absolute.path;
    // The infra recipe looks for the filename extension '.catapult_json',
    // so uploading to the Catapult performance dashboard is disabled if we
    // use a different extension.
    final catapultExtension =
        uploadEnabled ? '.catapult_json' : '.catapult_json_disabled';
    final outputFileName =
        _removeSuffix(resultsPath, '.fuchsiaperf.json') + catapultExtension;

    List<String> args = [
      '--input',
      result.absolute.path,
      '--output',
      outputFileName,
      '--execution-timestamp-ms',
      timestamp.toString(),
      '--masters',
      master,
      '--log-url',
      logurl,
      '--bots',
      bot
    ];
    if (releaseVersion != null) {
      args.addAll(['--product-versions', releaseVersion]);
    }

    final converter = Platform.script.resolve(converterPath).toFilePath();

    if (!await runProcess(converter, args)) {
      throw AssertionError('Running catapult_converter failed');
    }
    _log.info('Conversion to catapult results format completed.'
        ' Output file: $outputFileName');
  }

  /// Check that the performance test metrics in the given fuchsiaperf.json
  /// file follow some naming conventions.
  ///
  /// Check that the performance test metrics match the names listed in an
  /// expectations file.  This is currently optional.
  /// TODO(https://fxbug.dev/105202): Make this required.
  void _checkFuchsiaPerfMetricsNaming(File fuchsiaPerfFile,
      String expectedMetricNamesFile, Map<String, String> environment) {
    // The "test_suite" field should be all lower case.  It should start
    // with "fuchsia.", to distinguish Fuchsia test results from results
    // from other projects that upload to Catapult (Chromeperf), because
    // the namespace is shared between projects and Catapult does not
    // enforce any separation between projects.
    final testSuiteRegExp = RegExp(r'fuchsia\.([a-z0-9_-]+\.)*[a-z0-9_-]+$');

    // The regexp for the "label" field is fairly permissive.  This
    // reflects what is currently generated by tests.
    final labelRegExp = RegExp(r'[A-Za-z0-9_/.:=+<>\\ -]+$');

    final jsonData = jsonDecode(fuchsiaPerfFile.readAsStringSync());
    if (!(jsonData is List)) {
      throw ArgumentError('Top level fuchsiaperf node should be a list');
    }
    final List<String> errors = [];
    for (final Map<String, dynamic> entry in jsonData) {
      if (testSuiteRegExp.matchAsPrefix(entry['test_suite']) == null) {
        errors.add('test_suite field "${entry['test_suite']}"'
            ' does not match the pattern "${testSuiteRegExp.pattern}"');
      }
      if (labelRegExp.matchAsPrefix(entry['label']) == null) {
        errors.add('label field "${entry['label']}"'
            ' does not match the pattern "${labelRegExp.pattern}"');
      }
    }
    if (errors.isNotEmpty) {
      throw ArgumentError(
          "Some performance test metrics don't follow the naming conventions:\n"
          '${errors.join('\n')}');
    }

    if (expectedMetricNamesFile != null) {
      final Set<String> metrics = <String>{};
      for (final Map<String, dynamic> entry in jsonData) {
        metrics.add('${entry['test_suite']}: ${entry['label']}');
      }

      final updateDir = environment['FUCHSIA_EXPECTED_METRIC_NAMES_DEST_DIR'];
      if (updateDir == null) {
        // Normal case: Compare against the expectation file.
        final String runtimeDepsDir =
            Platform.script.resolve('runtime_deps').toFilePath();
        MetricsAllowlist(
                File(path.join(runtimeDepsDir, expectedMetricNamesFile)))
            .check(metrics);
      } else {
        // Special case: Update the expectation file.
        final destFile = File(path.join(updateDir, expectedMetricNamesFile));
        final list = List.from(metrics)..sort();
        destFile.writeAsStringSync(list.map((entry) => entry + '\n').join(''));
      }
    }
  }

  /// A helper function that runs a process with the given args.
  ///
  /// Used by the test to capture the parameters passed to [Process.run].
  ///
  /// Returns [true] if the process ran successfully, [false] otherwise.
  Future<bool> runProcess(String executablePath, List<String> args) async {
    _log.info('Performance: Running $executablePath ${args.join(" ")}');
    final ProcessResult results = await Process.run(executablePath, args);
    _log
      ..info(results.stdout)
      ..info(results.stderr);
    return results.exitCode == 0;
  }
}

class MetricsAllowlist {
  final String filename;
  final Set<String> expectedMetrics;

  MetricsAllowlist(File file)
      : filename = file.path,
        expectedMetrics = <String>{} {
    final String data = file.readAsStringSync();
    for (String line in LineSplitter.split(data)) {
      // Skip comment lines and empty lines.
      if (line.trim().startsWith('#') || line.trim() == '') continue;
      expectedMetrics.add(line);
    }
  }

  void check(Set<String> actualMetrics) {
    if (!SetEquality().equals(expectedMetrics, actualMetrics)) {
      final String diff = _formatSetDiff(expectedMetrics, actualMetrics);
      throw ArgumentError(
          'Metric names produced by the test differ from the expectations in'
          ' $filename:\n$diff\n\n'
          'One way to update the expectation file is to run the test locally'
          ' with this environment variable set:\n'
          'FUCHSIA_EXPECTED_METRIC_NAMES_DEST_DIR='
          '\$(pwd)/src/tests/end_to_end/perf/expected_metric_names\n\n'
          'See https://fuchsia.dev/fuchsia-src/development/performance/metric_name_expectations');
    }
  }
}

String _formatSetDiff(Set<String> set1, Set<String> set2) {
  List<String> union = List.from(set1.union(set2))..sort();
  final List<String> lines = [];
  for (final String entry in union) {
    final String tag =
        set2.contains(entry) ? (set1.contains(entry) ? ' ' : '+') : '-';
    lines.add(tag + entry);
  }
  return lines.join('\n');
}
