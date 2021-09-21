// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('MemoryMetricsProcessor');

class _Results {
  List<double> totalSystemMemory;
  List<double> vmoMemory;
  List<double> mmuMemory;
  List<double> ipcMemory;
  List<double> cpuBandwidth;
  List<double> gpuBandwidth;
  List<double> vdecBandwidth;
  List<double> vpuBandwidth;
  List<double> otherBandwidth;
  List<double> totalBandwidth;
  List<double> bandwidthUsage;
}

_Results _memoryMetrics(Model model, bool excludeBandwidth) {
  final memoryMonitorEvents =
      filterEvents(getAllEvents(model), category: 'memory_monitor');
  if (memoryMonitorEvents.isEmpty) {
    final duration = getTotalTraceDuration(model);
    _log.warning(
        'Could not find any `memory_monitor` events. Perhaps the trace '
        'duration (${duration.toMilliseconds()} milliseconds) is too short or '
        'the category "memory_monitor" is missing.');
    return null;
  }
  final totalMemory =
      filterEventsTyped<CounterEvent>(memoryMonitorEvents, name: 'fixed')
          ?.first
          ?.args['total'];
  if (totalMemory == null) {
    _log.warning(
        'Missing ("memory_monitor", "fixed") counter event in trace. No memory data is extracted.');
    return null;
  }
  final allocatedMemoryEvents =
      filterEventsTyped<CounterEvent>(memoryMonitorEvents, name: 'allocated');
  if (allocatedMemoryEvents.isEmpty) {
    final duration = getTotalTraceDuration(model);
    _log.warning(
        'Could not find any allocated memory events. Perhaps the trace '
        'duration (${duration.toMilliseconds()} milliseconds) is too short.');
    return null;
  }
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
  if (excludeBandwidth) {
    return _Results()
      ..totalSystemMemory = usedMemoryValues.toList()
      ..vmoMemory = vmoMemoryValues.toList()
      ..mmuMemory = mmuMemoryValues.toList()
      ..ipcMemory = ipcMemoryValues.toList();
  }
  final bandwidthUsageEvents = filterEventsTyped<CounterEvent>(
      memoryMonitorEvents,
      name: 'bandwidth_usage');
  if (bandwidthUsageEvents.isEmpty) {
    final duration = getTotalTraceDuration(model);
    _log.warning(
        'Could not find any allocated memory events. Perhaps the trace '
        'duration (${duration.toMilliseconds()} milliseconds) is too short.');
    return null;
  }
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
  return _Results()
    ..totalSystemMemory = usedMemoryValues.toList()
    ..vmoMemory = vmoMemoryValues.toList()
    ..mmuMemory = mmuMemoryValues.toList()
    ..ipcMemory = ipcMemoryValues.toList()
    ..cpuBandwidth = cpuBandwidthValues.toList()
    ..gpuBandwidth = gpuBandwidthValues.toList()
    ..vdecBandwidth = vdecBandwidthValues.toList()
    ..vpuBandwidth = vpuBandwidthValues.toList()
    ..otherBandwidth = otherBandwidthValues.toList()
    ..totalBandwidth = totalBandwidthValues.toList()
    ..bandwidthUsage = bandwidthUsagePercentValues.toList();
}

List<TestCaseResults> memoryMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final excludeBandwidth = extraArgs['exclude_bandwidth'] ?? false;
  final results = _memoryMetrics(model, excludeBandwidth);
  if (results == null) {
    return [];
  }

  _log
    ..info(
        'Average Total System Memory in bytes: ${computeMean(results.totalSystemMemory)}')
    ..info('Average VMO Memory in bytes: ${computeMean(results.vmoMemory)}')
    ..info(
        'Average MMU Overhead Memory in bytes: ${computeMean(results.mmuMemory)}')
    ..info('Average IPC Memory in bytes: ${computeMean(results.ipcMemory)}');

  if (!excludeBandwidth) {
    _log
      ..info(
          'Average CPU Memory Bandwidth Usage in bytes: ${computeMean(results.cpuBandwidth)}')
      ..info(
          'Average GPU Memory Bandwidth Usage in bytes: ${computeMean(results.gpuBandwidth)}')
      ..info(
          'Average VDEC Memory Bandwidth Usage in bytes: ${computeMean(results.vdecBandwidth)}')
      ..info(
          'Average VPU Memory Bandwidth Usage in bytes: ${computeMean(results.vpuBandwidth)}')
      ..info(
          'Average Other Memory Bandwidth Usage in bytes: ${computeMean(results.otherBandwidth)}')
      ..info(
          'Average Total Memory Bandwidth Usage in bytes: ${computeMean(results.totalBandwidth)}')
      ..info(
          'Average Memory Bandwidth Usage in percent: ${computeMean(results.bandwidthUsage)}')
      ..info(
          'Total bandwidth: ${(computeMean(results.totalBandwidth) / 1024).round()} KB/s, '
          'usage: ${computeMean(results.bandwidthUsage).toStringAsFixed(2)}%, '
          'cpu: ${(computeMean(results.cpuBandwidth) / 1024).round()} KB/s, '
          'gpu: ${(computeMean(results.gpuBandwidth) / 1024).round()} KB/s, '
          'vdec: ${(computeMean(results.vdecBandwidth) / 1024).round()} KB/s, '
          'vpu: ${(computeMean(results.vpuBandwidth) / 1024).round()} KB/s, '
          'other: ${(computeMean(results.otherBandwidth) / 1024).round()} KB/s');
  }

  return [
    TestCaseResults(
        'Total System Memory', Unit.bytes, results.totalSystemMemory.toList()),
    TestCaseResults('VMO Memory', Unit.bytes, results.vmoMemory.toList()),
    TestCaseResults(
        'MMU Overhead Memory', Unit.bytes, results.mmuMemory.toList()),
    TestCaseResults('IPC Memory', Unit.bytes, results.ipcMemory.toList()),
    if (!excludeBandwidth)
      TestCaseResults('CPU Memory Bandwidth Usage', Unit.bytes,
          results.cpuBandwidth.toList()),
    if (!excludeBandwidth)
      TestCaseResults('GPU Memory Bandwidth Usage', Unit.bytes,
          results.gpuBandwidth.toList()),
    if (!excludeBandwidth)
      TestCaseResults('VDEC Memory Bandwidth Usage', Unit.bytes,
          results.vdecBandwidth.toList()),
    if (!excludeBandwidth)
      TestCaseResults('VPU Memory Bandwidth Usage', Unit.bytes,
          results.vpuBandwidth.toList()),
    if (!excludeBandwidth)
      TestCaseResults('Other Memory Bandwidth Usage', Unit.bytes,
          results.otherBandwidth.toList()),
    if (!excludeBandwidth)
      TestCaseResults('Total Memory Bandwidth Usage', Unit.bytes,
          results.totalBandwidth.toList()),
    if (!excludeBandwidth)
      TestCaseResults('Memory Bandwidth Usage', Unit.percent,
          results.bandwidthUsage.toList())
  ];
}

String memoryMetricsReport(Model model) {
  final buffer = StringBuffer()..write('''
===
Memory
===

''');

  final results = _memoryMetrics(model, false);
  if (results != null) {
    buffer
      ..write('total_system_memory:\n')
      ..write(describeValues(results.totalSystemMemory, indent: 2))
      ..write('vmo_memory:\n')
      ..write(describeValues(results.vmoMemory, indent: 2))
      ..write('mmu_overhead_memory:\n')
      ..write(describeValues(results.mmuMemory, indent: 2))
      ..write('ipc_memory:\n')
      ..write(describeValues(results.ipcMemory, indent: 2))
      ..write('cpu_memory_bandwidth_usage:\n')
      ..write(describeValues(results.cpuBandwidth, indent: 2))
      ..write('gpu_memory_bandwidth_usage:\n')
      ..write(describeValues(results.gpuBandwidth, indent: 2))
      ..write('vdec_memory_bandwidth_usage:\n')
      ..write(describeValues(results.vdecBandwidth, indent: 2))
      ..write('vpu_memory_bandwidth_usage:\n')
      ..write(describeValues(results.vpuBandwidth, indent: 2))
      ..write('other_memory_bandwidth_usage:\n')
      ..write(describeValues(results.otherBandwidth, indent: 2))
      ..write('total_memory_bandwidth_usage:\n')
      ..write(describeValues(results.totalBandwidth, indent: 2))
      ..write('memory_bandwidth_usage:\n')
      ..write(describeValues(results.bandwidthUsage, indent: 2))
      ..write('\n');
  }
  return buffer.toString();
}
