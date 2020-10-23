// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

import 'metrics/cpu_metrics.dart';
import 'metrics/drm_fps.dart';
import 'metrics/flutter_frame_stats.dart';
import 'metrics/gpu_metrics.dart';
import 'metrics/input_latency.dart';
import 'metrics/memory_metrics.dart';
import 'metrics/scenic_frame_stats.dart';
import 'metrics/temperature_metrics.dart';
import 'metrics/total_trace_wall_time.dart';
import 'metrics_results.dart';
import 'trace_model.dart';

/// A specification of a metric.
class MetricsSpec {
  // The name of the metric.
  String name;

  // Additional metric-specific arguments.
  Map<String, dynamic> extraArgs = {};

  MetricsSpec({@required this.name, Map<String, dynamic> extraArgs}) {
    this.extraArgs = extraArgs ?? {};
  }
}

/// A collection of [MetricSpec]s.
///
/// Is tagged with a [testName] value to indicate what test name the
/// collection of computed metrics should be output under.
class MetricsSpecSet {
  String testSuite;
  String testName;
  List<MetricsSpec> metricsSpecs;

  MetricsSpecSet(
      {@required this.metricsSpecs,
      // TODO(fxbug.dev/59861): Make the testSuite argument required after transition
      // is done.
      this.testSuite,
      // TODO(fxbug.dev/59861): Make testName required after migration is done.
      this.testName}) {
    // TODO(fxbug.dev/59861): Remove the if block below, which is used for backward
    // compatible transition purpose.
    if (testName == null && testSuite != null) {
      testName = testSuite;
    }
  }
}

typedef MetricsProcessor = List<TestCaseResults> Function(
    Model, Map<String, dynamic> extraArgs);

const defaultMetricsRegistry = {
  'cpu': cpuMetricsProcessor,
  'drm_fps': drmFpsMetricsProcessor,
  'flutter_frame_stats': flutterFrameStatsMetricsProcessor,
  'gpu': gpuMetricsProcessor,
  'input_latency': inputLatencyMetricsProcessor,
  'memory': memoryMetricsProcessor,
  'scenic_frame_stats': scenicFrameStatsMetricsProcessor,
  'temperature': temperatureMetricsProcessor,
  'total_trace_wall_time': totalTraceWallTimeMetricsProcessor,
};

List<TestCaseResults> processMetrics(Model model, MetricsSpec metricsSpec,
    {Map<String, MetricsProcessor> registry = defaultMetricsRegistry}) {
  final processor = registry[metricsSpec.name];
  if (processor == null) {
    throw ArgumentError('Unknown metricsSpec "${metricsSpec.name}"');
  }

  return processor(model, metricsSpec.extraArgs);
}
