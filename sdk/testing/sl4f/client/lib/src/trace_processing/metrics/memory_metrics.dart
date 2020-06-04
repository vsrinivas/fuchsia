// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../time_delta.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('MemoryMetricsProcessor');

List<TestCaseResults> memoryMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final duration = getTotalTraceDuration(model);
  if (duration < TimeDelta.fromMilliseconds(1100)) {
    _log.info(
        'Trace duration (${duration.toMilliseconds()} milliseconds) is too short to provide Memory information');
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
  final vmoMemoryValues =
      getArgValuesFromEvents<num>(allocatedMemoryEvents, 'vmo')
          .map((v) => v.toDouble());
  final mmuMemoryValues =
      getArgValuesFromEvents<num>(allocatedMemoryEvents, 'mmu_overhead')
          .map((v) => v.toDouble());
  final ipcMemoryValues =
      getArgValuesFromEvents<num>(allocatedMemoryEvents, 'ipc')
          .map((v) => v.toDouble());
  final freeMemoryValues = getArgValuesFromEvents<num>(
          filterEventsTyped<CounterEvent>(memoryMonitorEvents, name: 'free'),
          'free')
      .map((v) => v.toDouble());
  final usedMemoryValues = freeMemoryValues.map((freeMemory) {
    final double usedMemory = totalMemory - freeMemory;
    return usedMemory;
  });
  final bandwidthUsageEvents = filterEventsTyped<CounterEvent>(
      memoryMonitorEvents,
      name: 'bandwidth_usage');
  final cpuBandwidthValues =
      getArgValuesFromEvents<num>(bandwidthUsageEvents, 'cpu')
          .map((v) => v.toDouble());
  final gpuBandwidthValues =
      getArgValuesFromEvents<num>(bandwidthUsageEvents, 'gpu')
          .map((v) => v.toDouble());
  final vdecBandwidthValues =
      getArgValuesFromEvents<num>(bandwidthUsageEvents, 'vdec')
          .map((v) => v.toDouble());
  final vpuBandwidthValues =
      getArgValuesFromEvents<num>(bandwidthUsageEvents, 'vpu')
          .map((v) => v.toDouble());
  final otherBandwidthValues =
      getArgValuesFromEvents<num>(bandwidthUsageEvents, 'other')
          .map((v) => v.toDouble());
  final totalBandwidthValues = bandwidthUsageEvents.map((e) {
    final num v = e.args['cpu'] +
        e.args['gpu'] +
        e.args['vdec'] +
        e.args['vpu'] +
        e.args['other'];
    return v.toDouble();
  });
  final bandwidthFreeEvents = filterEventsTyped<CounterEvent>(
      memoryMonitorEvents,
      name: 'bandwidth_free');
  final freeBandwidthValues =
      getArgValuesFromEvents<num>(bandwidthFreeEvents, 'value')
          .map((v) => v.toDouble());
  final bandwidthUsagePercentValues = Zip2Iterable<double, double, double>(
      totalBandwidthValues,
      freeBandwidthValues,
      (totalBandwidthValue, freeBandwidthValue) =>
          (100.0 * totalBandwidthValue) /
          (totalBandwidthValue + freeBandwidthValue));
  final List<TestCaseResults> results = [];
  <String, Iterable<double>>{
    'Total System Memory': usedMemoryValues,
    'VMO Memory': vmoMemoryValues,
    'MMU Overhead Memory': mmuMemoryValues,
    'IPC Memory': ipcMemoryValues,
    'CPU Memory Bandwidth Usage': cpuBandwidthValues,
    'GPU Memory Bandwidth Usage': gpuBandwidthValues,
    'VDEC Memory Bandwidth Usage': vdecBandwidthValues,
    'VPU Memory Bandwidth Usage': vpuBandwidthValues,
    'Other Memory Bandwidth Usage': otherBandwidthValues,
    'Total Memory Bandwidth Usage': totalBandwidthValues,
    'Memory Bandwidth Usage Percent': bandwidthUsagePercentValues,
  }.forEach((name, values) {
    _log.info('Average $name in bytes: ${computeMean(values)}');
    results.add(TestCaseResults(name, Unit.bytes, values.toList()));
  });
  return results;
}
