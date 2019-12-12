// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

import 'metrics/cpu_metrics.dart';
import 'metrics/drm_fps.dart';
import 'metrics/flutter_frame_stats.dart';
import 'metrics/input_latency.dart';
import 'metrics/memory_metrics.dart';
import 'metrics/scenic_frame_stats.dart';
import 'metrics/temperature_metrics.dart';
import 'metrics_results.dart';
import 'trace_model.dart';

/// A specification of a metric.
class MetricsSpec {
  // The name of the metric.
  String name;

  // Additional metric-specific arguments.
  Map<String, dynamic> extraArgs = {};

  MetricsSpec({@required this.name, this.extraArgs});
}

/// A collection of [MetricSpec]s.
///
/// Is tagged with a [testName] value to indicate what test name the
/// collection of computed metrics should be output under.
class MetricsSpecSet {
  String testName;
  List<MetricsSpec> metricsSpecs;

  MetricsSpecSet({@required this.testName, @required this.metricsSpecs});
}

typedef MetricsProcessor = List<TestCaseResults> Function(Model, MetricsSpec);

const defaultMetricsRegistry = {
  'cpu': cpuMetricsProcessor,
  'drm_fps': drmFpsMetricsProcessor,
  'flutter_frame_stats': flutterFrameStatsMetricsProcessor,
  'input_latency': inputLatencyMetricsProcessor,
  'memory': memoryMetricsProcessor,
  'scenic_frame_stats': scenicFrameStatsMetricsProcessor,
  'temperature': temperatureMetricsProcessor,
};

List<TestCaseResults> processMetrics(Model model, MetricsSpec metricsSpec,
    {Map<String, MetricsProcessor> registry = defaultMetricsRegistry}) {
  final processor = registry[metricsSpec.name];
  if (processor == null) {
    throw ArgumentError('Unknown metricsSpec "${metricsSpec.name}"');
  }

  return processor(model, metricsSpec);
}
