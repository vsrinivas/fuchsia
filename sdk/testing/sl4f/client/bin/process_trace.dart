// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:sl4f/trace_processing.dart';

// Metrics bundles are shorthand for predefined sets of metrics.
const _metricsBundles = {
  'ui': ['flutter_frame_stats', 'scenic_frame_stats'],
};

// A list of metrics that we support, along with their implementations.
const _metricsReporters = {
  'flutter_frame_stats': flutterFrameStatsReport,
  'scenic_frame_stats': scenicFrameStatsReport,
};

const _helpText = '''
Usage: process_trace.dart --metrics=<METRIC_OR_BUNDLE>,... <TRACE_FILE>
Process a trace file into metrics, and dump them to standard out.
Example: out/default/dart-tools/process_trace --metrics=ui trace.json
''';

void main(List<String> args) async {
  List<String> metricsOrBundles = [];
  String traceFilePath;

  for (final arg in args) {
    if (arg == '--help') {
      print(_helpText);
      return;
    } else if (arg.startsWith('--metrics=')) {
      final rest = arg.substring('--metrics='.length);
      metricsOrBundles.addAll(rest.split(','));
    } else if (!arg.startsWith('--')) {
      traceFilePath = arg;
    } else {
      print('Error: Encountered unknown arg: "$arg"');
      print(_helpText);
      return;
    }
  }

  if (traceFilePath == null) {
    print('Error, no trace file specified');
    print(_helpText);
    return;
  }

  if (metricsOrBundles.isEmpty) {
    print('Error, no values specified for --metrics=...');
    print(_helpText);
    return;
  }

  final List<String> metrics = [];
  for (final metricOrBundle in metricsOrBundles) {
    if (_metricsBundles.containsKey(metricOrBundle)) {
      metrics.addAll(_metricsBundles[metricOrBundle]);
    } else {
      metrics.add(metricOrBundle);
    }
  }

  print('Processing $traceFilePath using metrics: $metrics');

  final model = await createModelFromFilePath(traceFilePath);

  final result = StringBuffer();
  for (final metric in metrics) {
    final reporter = _metricsReporters[metric];
    if (reporter == null) {
      throw ArgumentError('Unsupported metric: $metric');
    }
    result.write(reporter(model));
  }

  print(result.toString());
}
