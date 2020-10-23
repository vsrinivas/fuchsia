// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

// This file implements the perf test results schema.
//
// See https://fuchsia.dev/fuchsia-src/development/benchmarking/results_schema
// for more details.

/// The set of valid Unit constants.
///
/// This should be kept in sync with the list of supported units in the
/// results schema docs linked at the top of this file.
enum Unit {
  // Time-based units.
  nanoseconds,
  milliseconds,

  // Size-based units.
  bytes,
  bytesPerSecond,

  // Frequency-based units.
  framesPerSecond,

  // Percentage-based units.
  percent,

  // Count-based units.
  count,

  // Power-based units.
  watts,
}

const _unitToCatapultConverterString = {
  Unit.nanoseconds: 'nanoseconds',
  Unit.milliseconds: 'milliseconds',
  Unit.bytes: 'bytes',
  Unit.bytesPerSecond: 'bytes/second',
  Unit.framesPerSecond: 'frames/second',
  Unit.percent: 'percent',
  Unit.count: 'count',
  Unit.watts: 'Watts',
};

/// Map [unit] to the corresponding string expected in catapult converter.
String unitToCatapultConverterString(Unit unit) =>
    _unitToCatapultConverterString[unit] ??
    (throw ArgumentError('Failed to map $unit to catapult converter string'));

/// TestCaseResults represents the results for a single test case.
///
/// See the link at the top of this file for documentation.
class TestCaseResults {
  String metric;
  // This field below is being renamed from "label" to "metric".
  // It is duplicated as a transitional measure so that it can be accessed via
  // either name.  TODO(fxbug.dev/59861): Remove the "label" field."
  String label;
  Unit unit;
  List<double> values;

  TestCaseResults(this.metric, this.unit, this.values) {
    // TODO(fxbug.dev/59861): Remove the statement below when "label" is removed.
    label = metric;
  }

  Map<String, dynamic> toJson({@required String testSuite}) => {
        'label': label,
        'test_suite': testSuite,
        'unit': unitToCatapultConverterString(unit),
        'values': values,
      };
}
