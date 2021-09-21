// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const _appName = 'flutter-test-app';
const _traceJsonString = '''
{
  "displayTimeUnit": "ns",
  "traceEvents": [
    {
      "cat": "flutter",
      "name": "StartComponent",
      "ts": 1000000,
      "pid": 50139,
      "tid": 50141,
      "ph": "X",
      "dur": 22783.271304347825,
      "args": {
        "url": "fuchsia-pkg://fuchsia.com/$_appName#meta/$_appName.cmx"
      }
    },
    {
      "cat": "flutter",
      "name": "vsync callback",
      "ts": 2000000.8,
      "pid": 50139,
      "tid": 205876,
      "ph": "X",
      "dur": 234567
    }
  ],
  "systemTraceEvents": {
    "type": "fuchsia",
    "events": [
      {
        "ph": "t",
        "pid": 50139,
        "tid": 205876,
        "name": "$_appName.cmx.ui"
      },
      {
        "ph": "t",
        "pid": 50139,
        "tid": 50141,
        "name": "io.flutter.runner.main"
      }
    ]
  }
}
''';
const _expectedStartupTime = 1234.5678;

const _emptyTraceJsonString = '''
{"displayTimeUnit":"ns","traceEvents":[],"systemTraceEvents":{"type":"fuchsia",
"events":[]}}
''';

void main() {
  final model = createModelFromJsonString(_traceJsonString);
  test('Flutter startup time metric', () {
    final List<TestCaseResults> results =
        FlutterTestHelper.flutterStartupTimeMetricsProcessor(
      model,
      {'flutterAppName': _appName},
    );
    expect(results.first.values.first, _expectedStartupTime);
  });

  test('Flutter startup time metric not given app name', () {
    expect(
        () => FlutterTestHelper.flutterStartupTimeMetricsProcessor(
              model,
              {},
            ),
        throwsArgumentError);
  });

  test('Flutter startup time metric given wrong app name', () {
    expect(
        () => FlutterTestHelper.flutterStartupTimeMetricsProcessor(
              model,
              {'flutterAppName': 'fake app name'},
            ),
        throwsArgumentError);
  });

  test('Flutter startup time metric given empty trace', () {
    final emptyModel = createModelFromJsonString(_emptyTraceJsonString);
    expect(
        () => FlutterTestHelper.flutterStartupTimeMetricsProcessor(
              emptyModel,
              {'flutterAppName': _appName},
            ),
        throwsArgumentError);
  });
}
