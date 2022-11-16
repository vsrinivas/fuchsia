// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io' show Platform;

import 'package:test/test.dart';

import 'helpers.dart';

const _catapultConverterPath = 'runtime_deps/catapult_converter';

void main() {
  enableLoggingOutput();

  test('storage-benchmarks', () async {
    final helper = await PerfTestHelper.make();
    final resultsFileFull = await helper.runTestComponentReturningResultsFile(
        packageName: 'storage-benchmarks',
        componentName: 'storage-benchmarks.cm',
        commandArgs:
            '--output-fuchsiaperf ${PerfTestHelper.componentOutputPath}',
        resultsFileSuffix: '');

    // Using the fuchsiaperf_full file like this avoids the processing done by
    // summarize.dart. This is for two reasons:
    // 1) To keep the initial iterations' times instead of dropping them
    //    (see src/storage/benchmarks/README.md).
    // 2) To allow standard deviations to be reported to Chromeperf so that
    //    Chromeperf displays them in its graphs.
    const fuchsiaPerfFullSuffix = 'fuchsiaperf_full.json';
    expect(resultsFileFull.path, endsWith(fuchsiaPerfFullSuffix));
    final resultsFile = await resultsFileFull.rename(resultsFileFull.path
        .replaceRange(
            resultsFileFull.path.length - fuchsiaPerfFullSuffix.length,
            null,
            'fuchsiaperf.json'));

    await helper.performance.convertResults(
        _catapultConverterPath, resultsFile, Platform.environment,
        expectedMetricNamesFile: 'fuchsia.storage.txt');
  }, timeout: Timeout.none);
}
