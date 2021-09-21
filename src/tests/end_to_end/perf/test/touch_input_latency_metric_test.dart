// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import 'helpers.dart';

const String _touchInputLatencyTraceJsonString = '''
{"displayTimeUnit":"ns","traceEvents":[{"cat":"touch-input-test","name":"input_latency","ts":63663083.949115049,"pid":34125,"tid":34127,"ph":"i","s":"p","args":{"input_injection_time":63652862796,"flutter_received_time":63656218403,"elapsed_time":3355607}},{"cat":"touch-input-test","name":"TouchInputTest::FlutterTap","ts":61108338.20575221,"pid":34125,"tid":34127,"ph":"X","dur":2554794.975663717}],"systemTraceEvents":{"type":"fuchsia","events":[{"ph":"t","pid":34125,"tid":34127,"name":"initial-thread"}]}}
''';

void main() {
  test('touch input latency metric', () async {
    final model = createModelFromJsonString(_touchInputLatencyTraceJsonString);
    final results = touchInputLatencyMetricsProcessor(model, {});

    expect(results[0].values[0], 3355607.0);
  });
}
