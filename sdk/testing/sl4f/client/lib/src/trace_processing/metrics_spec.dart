// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

import 'metrics/cpu_metrics.dart';
import 'metrics/drm_fps.dart';
import 'metrics/flutter_frame_stats.dart';
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

// TODO: In the future we could refactor the metrics registry into something
// more dynamic, if there's a need for it.
List<TestCaseResults> processWithDefaultMetricsRegistry(
    Model model, MetricsSpec metricsSpec) {
  const mapping = {
    'flutter_frame_stats': flutterFrameStatsMetricsProcessor,
    'scenic_frame_stats': scenicFrameStatsMetricsProcessor,
    'drm_fps': drmFpsMetricsProcessor,
    'cpu': cpuMetricsProcessor,
    'memory': memoryMetricsProcessor,
    'temperature': temperatureMetricsProcessor,
  };

  final processor = mapping[metricsSpec.name];
  if (processor == null) {
    throw ArgumentError('Unknown metricsSpec "${metricsSpec.name}"');
  }

  return processor(model, metricsSpec);
}
