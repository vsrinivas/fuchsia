// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../metrics_spec.dart';
import '../time_delta.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('MemoryMetricsProcessor');

List<TestCaseResults> memoryMetricsProcessor(
    Model model, MetricsSpec metricsSpec) {
  if (metricsSpec.name != 'memory') {
    throw ArgumentError(
        'Error, unexpected metrics name "${metricsSpec.name}" in '
        'memoryMetricsProcessor');
  }
  final duration = getTotalTraceDuration(model);
  if (duration < TimeDelta.fromMilliseconds(1100)) {
    _log.info(
        'Trace duration (${duration.toMilliseconds()} millisecconds) is too short to provide Memory information');
    return [];
  }
  final memoryMonitorEvents =
      filterEvents(getAllEvents(model), category: 'memory_monitor');
  if (memoryMonitorEvents.isEmpty) {
    _log.warning(
        'Missing category "memory_monitor" events in trace. No memory data is extracted.');
    return [];
  }
  final totalMemory =
      filterEventsTyped<CounterEvent>(memoryMonitorEvents, name: 'fixed')
          ?.first
          ?.args['total'];
  if (totalMemory == null) {
    _log.warning(
        'Missing ("memory_monitor", "fixed") counter event in trace. No memory data is extracted.');
    return [];
  }
  final allocatedMemoryEvents =
      filterEventsTyped<CounterEvent>(memoryMonitorEvents, name: 'allocated');
  final vmoMemoryValues = getArgsFromEvents<num>(allocatedMemoryEvents, 'vmo')
      .map((v) => v.toDouble());
  final mmuMemoryValues =
      getArgsFromEvents<num>(allocatedMemoryEvents, 'mmu_overhead')
          .map((v) => v.toDouble());
  final ipcMemoryValues = getArgsFromEvents<num>(allocatedMemoryEvents, 'ipc')
      .map((v) => v.toDouble());
  final freeMemoryValues = getArgsFromEvents<num>(
          filterEventsTyped<CounterEvent>(memoryMonitorEvents, name: 'free'),
          'free')
      .map((v) => v.toDouble());
  final usedMemoryValues = freeMemoryValues.map((freeMemory) {
    final double usedMemory = totalMemory - freeMemory;
    return usedMemory;
  });
  final List<TestCaseResults> results = [];
  <String, Iterable<double>>{
    'Total System Memory': usedMemoryValues,
    'VMO Memory': vmoMemoryValues,
    'MMU Overhead Memory': mmuMemoryValues,
    'IPC Memory': ipcMemoryValues,
  }.forEach((name, values) {
    _log.info('Average $name in bytes: ${computeMean(values)}');
    results.add(TestCaseResults(name, Unit.bytes, values.toList()));
  });
  return results;
}
