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

  Map<String, List<double>> bandwidthChannels;
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

  final fixedMemoryEvents =
      filterEventsTyped<CounterEvent>(memoryMonitorEvents, name: 'fixed');
  if (fixedMemoryEvents.isEmpty) {
    _log.warning(
        'Missing ("memory_monitor", "fixed") counter event in trace. No memory data is extracted.');
    return null;
  }
  final totalMemory = fixedMemoryEvents.first.args['total'];
  if (totalMemory == null) {
    _log.warning(
        'Malformed ("memory_monitor", "fixed") counter event in trace. Missing "total" field. No memory data is extracted.');
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

  final bandwidthChannels = <String, List<double>>{};
  for (final event in bandwidthUsageEvents) {
    for (final entry in event.args.entries) {
      bandwidthChannels[entry.key] ??= <double>[];
      bandwidthChannels[entry.key].add(entry.value.toDouble());
    }
  }

  final totalBandwidthValues = bandwidthUsageEvents
      .map((e) => e.args.values.fold(0.0, (a, b) => a + b).toDouble())
      .cast<double>();

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
    ..bandwidthChannels = bandwidthChannels
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
          'Average Total Memory Bandwidth Usage in bytes: ${computeMean(results.totalBandwidth)}')
      ..info(
          'Average Memory Bandwidth Usage in percent: ${computeMean(results.bandwidthUsage)}');
    for (final entry in results.bandwidthChannels.entries) {
      _log.info(
          'Average ${entry.key} bandwidth usage in bytes: ${computeMean(entry.value)}');
    }
    _log.info([
      'Total bandwidth: ${(computeMean(results.totalBandwidth) / 1024).round()} KB/s',
      'usage: ${computeMean(results.bandwidthUsage).toStringAsFixed(2)}%',
      for (final entry in results.bandwidthChannels.entries)
        '${entry.key}: ${(computeMean(entry.value) / 1024).round()} KB/s',
    ].join(', '));
  }

  // Map bandwidth usage event field names to their metric name.  "Other" is
  // capitalized and all other names are mapped to full upper case
  // (e.g. "cpu" -> "CPU").  This logic is especially important for preserving
  // existing metric names from before we accepted arbitrary bandwidth usage
  // event fields.
  String _toMetricName(String traceName) {
    if (traceName == 'other') {
      return 'Other';
    }
    return traceName.toUpperCase();
  }

  return [
    TestCaseResults(
        'Total System Memory', Unit.bytes, results.totalSystemMemory.toList()),
    TestCaseResults('VMO Memory', Unit.bytes, results.vmoMemory.toList()),
    TestCaseResults(
        'MMU Overhead Memory', Unit.bytes, results.mmuMemory.toList()),
    TestCaseResults('IPC Memory', Unit.bytes, results.ipcMemory.toList()),
    if (!excludeBandwidth)
      for (final entry
          in results.bandwidthChannels.entries.toList()
            ..sort((a, b) => a.key.compareTo(b.key)))
        TestCaseResults('${_toMetricName(entry.key)} Memory Bandwidth Usage',
            Unit.bytes, entry.value.toList()),
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
      ..write(describeValues(results.ipcMemory, indent: 2));
    for (final entry in results.bandwidthChannels.entries) {
      buffer
        ..write('${entry.key}_memory_bandwidth_usage:\n')
        ..write(describeValues(entry.value, indent: 2));
    }
    buffer
      ..write('total_memory_bandwidth_usage:\n')
      ..write(describeValues(results.totalBandwidth, indent: 2))
      ..write('memory_bandwidth_usage:\n')
      ..write(describeValues(results.bandwidthUsage, indent: 2))
      ..write('\n');
  }
  return buffer.toString();
}
