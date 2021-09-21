// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

double _totalTraceWallTimeMillis(Model model) =>
    getTotalTraceDuration(model).toMillisecondsF();

List<TestCaseResults> totalTraceWallTimeMetricsProcessor(
        Model model, Map<String, dynamic> _extraArgs) =>
    [
      TestCaseResults('Total Trace Wall Time', Unit.milliseconds,
          [_totalTraceWallTimeMillis(model)]),
    ];

String totalTraceWallTimeReport(Model model) {
  final totalTraceWallTimeMillis = _totalTraceWallTimeMillis(model);

  return 'Total trace wall time: $totalTraceWallTimeMillis ms\n';
}
