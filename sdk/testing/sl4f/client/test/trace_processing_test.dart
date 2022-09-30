// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9
import 'dart:io' show Platform;

import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

Model _getTestModel() {
  final readEvent = DurationEvent(
      TimeDelta.fromMicroseconds(698607461.7395687) -
          TimeDelta.fromMicroseconds(697503138.9531089),
      null,
      [],
      [],
      'io',
      'Read',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697503138.9531089)),
      7009,
      7021,
      {});

  final writeEvent = DurationEvent(
      TimeDelta.fromMicroseconds(697868582.5994568) -
          TimeDelta.fromMicroseconds(697778328.2160872),
      null,
      [],
      [],
      'io',
      'Write',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697778328.2160872)),
      7009,
      7022,
      {});

  final asyncReadWriteEvent = AsyncEvent(
      43,
      TimeDelta.fromMicroseconds(698607461.0) -
          TimeDelta.fromMicroseconds(697503138.0),
      'io',
      'AsyncReadWrite',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697503138)),
      7009,
      7022,
      {});

  final readEvent2 = DurationEvent(
    TimeDelta.fromMicroseconds(697868571.6018075) -
        TimeDelta.fromMicroseconds(697868185.3588456),
    null,
    [],
    [],
    'io',
    'Read',
    TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697868185.3588456)),
    7010,
    7023,
    {},
  );

  final flowStart = FlowEvent(
      '0',
      FlowEventPhase.start,
      null,
      null,
      null,
      'io',
      'ReadWriteFlow',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697503139.9531089)),
      7009,
      7021, {});

  final flowStep = FlowEvent(
      '0',
      FlowEventPhase.step,
      null,
      null,
      null,
      'io',
      'ReadWriteFlow',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697779328.2160872)),
      7009,
      7022, {});

  final flowEnd = FlowEvent(
      '0',
      FlowEventPhase.end,
      null,
      null,
      null,
      'io',
      'ReadWriteFlow',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697868050.2160872)),
      7009,
      7022, {});

  final counterEvent = CounterEvent(
      null,
      'system_metrics',
      'cpu_usage',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(698607465.375)),
      7010,
      7023,
      {'average_cpu_percentage': 0.89349317793, 'max_cpu_usage': 0.1234});

  final instantEvent = InstantEvent(
      InstantEventScope.global,
      'log',
      'log',
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(698607465.312)),
      7009,
      7021,
      {'message': '[INFO:trace_manager.cc(66)] Stopping trace'});

  flowStart
    ..enclosingDuration = readEvent
    ..previousFlow = null
    ..nextFlow = flowStep;

  flowStep
    ..enclosingDuration = writeEvent
    ..previousFlow = flowStart
    ..nextFlow = flowEnd;

  flowEnd
    ..enclosingDuration = writeEvent
    ..previousFlow = flowStep
    ..nextFlow = null;

  readEvent.childFlows = [flowStart];
  writeEvent.childFlows = [flowStep, flowEnd];

  final thread7021 = Thread(7021, events: [readEvent, flowStart, instantEvent]);

  final thread7022 = Thread(7022,
      name: 'initial-thread',
      events: [asyncReadWriteEvent, writeEvent, flowStep, flowEnd]);

  final thread7023 = Thread(7023, events: [readEvent2, counterEvent]);

  final process7009 =
      Process(7009, name: 'root_presenter', threads: [thread7021, thread7022]);

  final process7010 = Process(7010, threads: [thread7023]);

  final model = Model()..processes = [process7009, process7010];

  return model;
}

Map<String, dynamic> _toDictionary(Event e) {
  final result = {
    'cat': e.category,
    'name': e.name,
    'start.toEpochDelta().toNanoseconds()':
        e.start.toEpochDelta().toNanoseconds(),
    'pid': e.pid,
    'tid': e.tid,
    'args': e.args
  };
  if (e is InstantEvent) {
    result['scope'] = e.scope;
  } else if (e is CounterEvent) {
    final id = e.id;
    if (id != null) {
      result['id'] = id;
    }
  } else if (e is DurationEvent) {
    result['duration.toNanoseconds()'] = e.duration.toNanoseconds();
    result['!!parent'] = e.parent != null;
    result['childDurations.length'] = e.childDurations.length;
    result['childFlows.length'] = e.childFlows.length;
  } else if (e is AsyncEvent) {
    result['id'] = e.id;
    final duration = e.duration;
    if (duration != null) {
      result['duration'] = duration;
    }
  } else if (e is FlowEvent) {
    result['id'] = e.id;
    result['phase'] = e.phase;
    result['!!enclosingDuration'] = e.enclosingDuration != null;
    result['!!previousFlow'] = e.previousFlow != null;
    result['!!nextFlow'] = e.nextFlow != null;
  } else {
    fail('Unexpected Event type ${e.runtimeType} in |_toDictionary(Event)|');
  }
  return result;
}

void _checkEventsEqual(Event a, Event b) {
  var result = a.category == b.category &&
      a.name == b.name &&
      a.start == b.start &&
      a.pid == b.pid &&
      a.tid == b.tid;

  // The [args] field of an [Event] should never be null.
  expect(a.args, isNotNull);
  expect(b.args, isNotNull);

  // Note: Rather than trying to handling the possibly complicated object
  // structure on each event here for equality, we just verify that their
  // key sets are equal.  This is safe, as this function is only used for
  // testing, rather than publicy exposed.
  result &= a.args.length == b.args.length &&
      a.args.keys.toSet().containsAll(b.args.keys);

  if (a is InstantEvent && b is InstantEvent) {
    result &= a.scope == b.scope;
  } else if (a is CounterEvent && b is CounterEvent) {
    result &= a.id == b.id;
  } else if (a is DurationEvent && b is DurationEvent) {
    expect(a.duration.toMicroseconds(), _closeTo(b.duration.toMicroseconds()));
    result &= (a.parent == null) == (b.parent == null);
    result &= a.childDurations.length == b.childDurations.length;
    result &= a.childFlows.length == b.childFlows.length;
  } else if (a is AsyncEvent && b is AsyncEvent) {
    result &= a.id == b.id;
    expect(a.duration.toMicroseconds(), _closeTo(b.duration.toMicroseconds()));
  } else if (a is FlowEvent && b is FlowEvent) {
    result &= a.id == b.id;
    result &= a.phase == b.phase;
    expect(a.enclosingDuration, isNotNull);
    expect(b.enclosingDuration, isNotNull);
    result &= (a.previousFlow == null) == (b.previousFlow == null);
    result &= (a.nextFlow == null) == (b.nextFlow == null);
  } else {
    // We hit this case if the types don't match.
    result &= false;
  }

  if (!result) {
    fail(
        'Error, event $a ${_toDictionary(a)} not equal to event $b ${_toDictionary(b)}');
  }
}

void _checkThreadsEqual(Thread a, Thread b) {
  if (a.tid != b.tid) {
    fail('Error, thread tids did match: ${a.tid} vs ${b.tid}');
  }
  if (a.name != b.name) {
    fail(
        'Error, thread names (tid=${a.tid}) did not match: ${a.name} vs ${b.name}');
  }
  if (a.events.length != b.events.length) {
    fail(
        'Error, thread (tid=${a.tid}, name=${a.name}) events lengths did not match: ${a.events.length} vs ${b.events.length}');
  }
  for (int i = 0; i < a.events.length; i++) {
    _checkEventsEqual(a.events[i], b.events[i]);
  }
}

void _checkProcessesEqual(Process a, Process b) {
  if (a.pid != b.pid) {
    fail('Error, process pids did match: ${a.pid} vs ${b.pid}');
  }
  if (a.name != b.name) {
    fail(
        'Error, process (pid=${a.pid}) names did not match: ${a.name} vs ${b.name}');
  }
  if (a.threads.length != b.threads.length) {
    fail(
        'Error, process (pid=${a.pid}, name=${a.name}) threads lengths did not match: ${a.threads.length} vs ${b.threads.length}');
  }
  for (int i = 0; i < a.threads.length; i++) {
    _checkThreadsEqual(a.threads[i], b.threads[i]);
  }
}

void _checkModelsEqual(Model a, Model b) {
  if (a.processes.length != b.processes.length) {
    fail(
        'Error, model processes lengths did not match: ${a.processes.length} vs ${b.processes.length}');
  }
  for (int i = 0; i < a.processes.length; i++) {
    _checkProcessesEqual(a.processes[i], b.processes[i]);
  }
}

Matcher _closeTo(num value, {num delta = 1e-5}) => closeTo(value, delta);

Future<Model> _modelFromPath(String path) =>
    createModelFromFilePath(Platform.script.resolve(path).toFilePath());

void main(List<String> args) {
  test('Create trace model', () async {
    final testModel = _getTestModel();
    final testModelFromJson = await _modelFromPath('runtime_deps/model.json');
    _checkModelsEqual(testModel, testModelFromJson);
  });

  test('Dangling begin event', () async {
    final model = createModelFromJsonString('''
{
  "displayTimeUnit": "ns",
  "traceEvents": [
    {
      "cat": "category",
      "name": "name",
      "ts": 0.0,
      "ph": "B",
      "tid": 0,
      "pid": 0
    }
  ],
  "systemTraceEvents": {
    "events": [],
    "type": "fuchsia"
  }
}
''');
    expect(getAllEvents(model), isEmpty);
  });

  test('Filter events', () async {
    final events = [
      DurationEvent(
          null,
          null,
          [],
          [],
          'cat_a',
          'name_a',
          TimePoint.fromEpochDelta(
              TimeDelta.fromMicroseconds(697778328.2160872)),
          7009,
          7022,
          {}),
      DurationEvent(
          null,
          null,
          [],
          [],
          'cat_b',
          'name_b',
          TimePoint.fromEpochDelta(
              TimeDelta.fromMicroseconds(697778328.2160872)),
          7009,
          7022,
          {})
    ];

    final filtered = filterEvents(events, category: 'cat_a', name: 'name_a');
    expect(filtered, equals([events.first]));

    final filtered2 =
        filterEvents(events, category: 'cat_c', name: 'name_c').toList();
    expect(filtered2, equals([]));
  });

  test('Filter events typed', () async {
    final events = [
      DurationEvent(
          null,
          null,
          [],
          [],
          'cat_a',
          'name_a',
          TimePoint.fromEpochDelta(
              TimeDelta.fromMicroseconds(697778328.2160872)),
          7009,
          7022,
          {}),
      DurationEvent(
          null,
          null,
          [],
          [],
          'cat_b',
          'name_b',
          TimePoint.fromEpochDelta(
              TimeDelta.fromMicroseconds(697778328.2160872)),
          7009,
          7022,
          {})
    ];

    final filtered = filterEventsTyped<DurationEvent>(events,
        category: 'cat_a', name: 'name_a');
    expect(filtered, equals([events.first]));

    final filtered2 = filterEventsTyped<DurationEvent>(events,
        category: 'cat_c', name: 'name_c');
    expect(filtered2, equals([]));

    final filtered3 = filterEventsTyped<InstantEvent>(events,
        category: 'cat_a', name: 'name_a');
    expect(filtered3, equals([]));
  });

  test('Compute stats', () async {
    expect(computeMean([1.0, 2.0, 3.0]), _closeTo(2.0));

    expect(computeVariance([1.0, 2.0, 3.0]), _closeTo(0.6666666666666666));

    expect(
        computeStandardDeviation([1.0, 2.0, 3.0]), _closeTo(0.816496580927726));
    expect(computePercentile([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0], 25),
        _closeTo(3.0));
    expect(computePercentile([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0], 50),
        _closeTo(5.0));
    expect(computePercentile([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0], 75),
        _closeTo(7.0));
    expect(
        computePercentile(
            [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0], 25),
        _closeTo(3.25));
    expect(
        computePercentile(
            [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0], 50),
        _closeTo(5.5));
    expect(
        computePercentile(
            [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0], 75),
        _closeTo(7.75));

    expect(computeMax([1.0, 2.0, 3.0]), _closeTo(3.0));
    expect(computeMin([1.0, 2.0, 3.0]), _closeTo(1.0));

    expect(differenceValues([1.0, 2.0, 3.0]), equals([1.0, 1.0]));
  });

  test('Test discrepancy calculation', () async {
    // The sample sequences from section 3 of
    // https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/45361.pdf
    expect(computeDiscrepancy([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]),
        _closeTo(1.0));
    expect(computeDiscrepancy([1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12]),
        _closeTo(2.0));
    // The paper linked above has an error in the third example. The true value
    // is 23/9, which is the overshoot from the interval [4, 10], while the
    // stated value of 2 is the undershoot from e.g. (2, 4).
    expect(computeDiscrepancy([1, 2, 4, 5, 6, 7, 8, 9, 10, 12]),
        _closeTo(23.0 / 9.0));
    expect(computeDiscrepancy([1, 2, 4, 6, 7, 8, 9, 10, 11, 12]),
        _closeTo(25.0 / 9.0));
    expect(
        computeDiscrepancy([1, 2, 5, 6, 7, 8, 9, 10, 11, 12]), _closeTo(3.0));

    // Check that small sample counts return zero.
    expect(computeDiscrepancy([]), _closeTo(0.0));
    expect(computeDiscrepancy([1]), _closeTo(0.0));
  });

  test('Flutter frame stats metric', () async {
    final model = await _modelFromPath('runtime_deps/flutter_app.json');
    final results = flutterFrameStatsMetricsProcessor(
        model, {'flutterAppName': 'flutter_app'});

    expect(results[0].label, 'flutter_app_fps');
    expect(results[0].values[0], _closeTo(53.999868151104536));
    expect(results[1].label, 'flutter_app_frame_build_times');
    expect(computeMean(results[1].values), _closeTo(1.5090477545454541));
    expect(results[2].label, 'flutter_app_frame_rasterizer_times');
    expect(computeMean(results[2].values), _closeTo(4.803573118483413));
    expect(results[3].label, 'flutter_app_frame_latencies');
    expect(computeMean(results[3].values), _closeTo(34.17552801895736));
    expect(results[4].label, 'flutter_app_render_frame_total_durations');
    expect(computeMean(results[4].values), _closeTo(10.729088445497638));
    expect(results[5].label, 'flutter_app_frame_time_discrepancy');
    expect(results[5].values[0], _closeTo(300.633182));
    expect(results[6].label, 'flutter_app_undisplayed_frame_count');
    expect(results[6].values[0], 4.0);
  });

  test('Flutter frame stats with long name app', () async {
    final model =
        await _modelFromPath('runtime_deps/flutter_app_long_name.json');
    final results = flutterFrameStatsMetricsProcessor(
        model, {'flutterAppName': 'flutter_app_long_name_xy'});

    expect(results[0].label, 'flutter_app_long_name_xy_fps');
    expect(results[0].values[0], _closeTo(53.999868151104536));
    expect(results[1].label, 'flutter_app_long_name_xy_frame_build_times');
    expect(computeMean(results[1].values), _closeTo(1.5090477545454541));
    expect(results[2].label, 'flutter_app_long_name_xy_frame_rasterizer_times');
    expect(computeMean(results[2].values), _closeTo(4.803573118483413));
    expect(results[3].label, 'flutter_app_long_name_xy_frame_latencies');
    expect(computeMean(results[3].values), _closeTo(34.17552801895736));
    expect(results[4].label,
        'flutter_app_long_name_xy_render_frame_total_durations');
    expect(computeMean(results[4].values), _closeTo(10.729088445497638));
    expect(
        results[6].label, 'flutter_app_long_name_xy_undisplayed_frame_count');
    expect(results[6].values[0], 4.0);
  });

  test('Flutter frame stats metric (no Scenic edge case)', () async {
    final model =
        await _modelFromPath('runtime_deps/flutter_app_no_scenic.json');
    expect(
        () => flutterFrameStatsMetricsProcessor(
            model, {'flutterAppName': 'flutter_app'}),
        throwsA(isArgumentError));
  });

  test('Scenic frame stats metric', () async {
    final model = await _modelFromPath('runtime_deps/scenic.json');
    final results = scenicFrameStatsMetricsProcessor(model, {});

    expect(computeMean(results[0].values), _closeTo(1.254430254));
    expect(computeMean(results[1].values), _closeTo(3.649910802));
    expect(computeMean(results[2].values), _closeTo(0.849043290));
  });

  test('Scenic frame stats metric (no connected frames edge case)', () async {
    final model = createModelFromJsonString('''
{
  "displayTimeUnit": "ns",
  "traceEvents": [
    {
      "cat": "gfx",
      "name": "ApplyScheduledSessionUpdates",
      "ts": 12345,
      "pid": 35204,
      "tid": 323993,
      "ph": "X",
      "dur": 200
    }
  ],
  "systemTraceEvents": {
    "events": [],
    "type": "fuchsia"
  }
}
''');

    final results = scenicFrameStatsMetricsProcessor(model, {});

    expect(results[0].values, equals([0.0]));
    expect(results[1].values, equals([0.0]));
    expect(results[2].values, equals([0.0]));
  });

// TODO(fxbug.dev/73367): Modify this test to only include VsyncProcessCallback
  group('DRM FPS metric for Vsync callback', () {
    final dataFiles = [
      'runtime_deps/flutter_app_vsync_process_callback.json',
      'runtime_deps/flutter_app.json'
    ];
    for (final file in dataFiles) {
      test('$file', () async {
        final model = await _modelFromPath(file);
        final results =
            drmFpsMetricsProcessor(model, {'flutterAppName': 'flutter_app'});
        expect(computeMean(results[0].values), _closeTo(57.72797479950718));
        expect(results[1].values[0], _closeTo(59.954463866487885));
        expect(results[2].values[0], _closeTo(59.997900074985296));
        expect(results[3].values[0], _closeTo(60.034055041976686));
      });
    }
  });

// TODO(fxbug.dev/73367): Modify this test to only include VsyncProcessCallback
  group('System DRM FPS metric for vsync callback', () {
    final dataFiles = [
      'runtime_deps/flutter_app_vsync_process_callback.json',
      'runtime_deps/flutter_app.json'
    ];
    for (final file in dataFiles) {
      test('$file', () async {
        final model = await _modelFromPath(file);
        final results = systemDrmFpsMetricsProcessor(model, {});

        expect(computeMean(results[0].values), _closeTo(53.22293098104574));
        expect(results[1].values[0], _closeTo(20.00118695220081));
        expect(results[2].values[0], _closeTo(59.99295082827768));
        expect(results[3].values[0], _closeTo(60.03226494111525));
      });
    }
  });

  test('CPU metric', () async {
    final model = await _modelFromPath('runtime_deps/cpu_metric.json');
    final results = cpuMetricsProcessor(model, {});
    expect(results[0].values[0], _closeTo(43.00));
    expect(results[0].values[1], _closeTo(20.00));
    final aggregatedResults =
        cpuMetricsProcessor(model, {'aggregateMetricsOnly': true});
    expect(aggregatedResults.length, equals(8));
    expect(aggregatedResults[0].label, equals('cpu_p5'));
    expect(aggregatedResults[0].values[0], _closeTo(21.15));
    expect(aggregatedResults[1].label, equals('cpu_p25'));
    expect(aggregatedResults[1].values[0], _closeTo(25.75));
    expect(aggregatedResults[2].label, equals('cpu_p50'));
    expect(aggregatedResults[2].values[0], _closeTo(31.50));
    expect(aggregatedResults[3].label, equals('cpu_p75'));
    expect(aggregatedResults[3].values[0], _closeTo(37.25));
    expect(aggregatedResults[4].label, equals('cpu_p95'));
    expect(aggregatedResults[4].values[0], _closeTo(41.85));
    expect(aggregatedResults[5].label, equals('cpu_min'));
    expect(aggregatedResults[5].values[0], _closeTo(20.00));
    expect(aggregatedResults[6].label, equals('cpu_max'));
    expect(aggregatedResults[6].values[0], _closeTo(43.00));
    expect(aggregatedResults[7].label, equals('cpu_average'));
    expect(aggregatedResults[7].values[0], _closeTo(31.50));
  });

  test('CPU metric after system metrics logger migration', () async {
    final model = await _modelFromPath(
        'runtime_deps/cpu_metric_system_metrics_logger.json');
    final results = cpuMetricsProcessor(model, {});
    expect(results[0].values[0], _closeTo(43.00));
    expect(results[0].values[1], _closeTo(20.00));
    final aggregatedResults =
        cpuMetricsProcessor(model, {'aggregateMetricsOnly': true});
    expect(aggregatedResults.length, equals(8));
    expect(aggregatedResults[0].label, equals('cpu_p5'));
    expect(aggregatedResults[0].values[0], _closeTo(21.15));
    expect(aggregatedResults[1].label, equals('cpu_p25'));
    expect(aggregatedResults[1].values[0], _closeTo(25.75));
    expect(aggregatedResults[2].label, equals('cpu_p50'));
    expect(aggregatedResults[2].values[0], _closeTo(31.50));
    expect(aggregatedResults[3].label, equals('cpu_p75'));
    expect(aggregatedResults[3].values[0], _closeTo(37.25));
    expect(aggregatedResults[4].label, equals('cpu_p95'));
    expect(aggregatedResults[4].values[0], _closeTo(41.85));
    expect(aggregatedResults[5].label, equals('cpu_min'));
    expect(aggregatedResults[5].values[0], _closeTo(20.00));
    expect(aggregatedResults[6].label, equals('cpu_max'));
    expect(aggregatedResults[6].values[0], _closeTo(43.00));
    expect(aggregatedResults[7].label, equals('cpu_average'));
    expect(aggregatedResults[7].values[0], _closeTo(31.50));
  });

  test('GPU metric', () async {
    {
      final model = await _modelFromPath('runtime_deps/gpu_utilization.json');
      final results = gpuMetricsProcessor(model, {});
      expect(computeMean(results[0].values), _closeTo(20.43815763249877));
    }

    {
      final model =
          await _modelFromPath('runtime_deps/gpu_utilization_sub_second.json');
      final results = gpuMetricsProcessor(model, {});
      expect(computeMean(results[0].values), _closeTo(80.0));
    }

    {
      final model = await _modelFromPath(
          'runtime_deps/gpu_utilization_super_second.json');
      final results = gpuMetricsProcessor(model, {});
      expect(computeMean(results[0].values), _closeTo(70.0));
    }
  });

  test('Temperature metric with metrics_logger data', () async {
    final model = await _modelFromPath('runtime_deps/temperature_metric.json');
    final results = temperatureMetricsProcessor(model, {});
    expect(results[0].values[0], _closeTo(48.90));
    expect(results[0].values[1], _closeTo(47.90));
    final aggregatedResults =
        temperatureMetricsProcessor(model, {'aggregateMetricsOnly': true});
    expect(aggregatedResults.length, equals(8));
    expect(aggregatedResults[0].label, equals('temperature_p5'));
    expect(aggregatedResults[0].values[0], _closeTo(47.95));
    expect(aggregatedResults[1].label, equals('temperature_p25'));
    expect(aggregatedResults[1].values[0], _closeTo(48.15));
    expect(aggregatedResults[2].label, equals('temperature_p50'));
    expect(aggregatedResults[2].values[0], _closeTo(48.40));
    expect(aggregatedResults[3].label, equals('temperature_p75'));
    expect(aggregatedResults[3].values[0], _closeTo(48.65));
    expect(aggregatedResults[4].label, equals('temperature_p95'));
    expect(aggregatedResults[4].values[0], _closeTo(48.85));
    expect(aggregatedResults[5].label, equals('temperature_min'));
    expect(aggregatedResults[5].values[0], _closeTo(47.90));
    expect(aggregatedResults[6].label, equals('temperature_max'));
    expect(aggregatedResults[6].values[0], _closeTo(48.90));
    expect(aggregatedResults[7].label, equals('temperature_average'));
    expect(aggregatedResults[7].values[0], _closeTo(48.40));
  });

  test('Memory metric', () async {
    final model = await _modelFromPath('runtime_deps/memory_metric.json');
    final results = memoryMetricsProcessor(model, {});
    expect(results[0].label, equals('Total System Memory'));
    expect(results[0].values[0], _closeTo(940612736));
    expect(results[0].values[1], _closeTo(990612736));
    expect(results[1].label, equals('VMO Memory'));
    expect(results[1].values[0], _closeTo(781942784));
    expect(results[1].values[1], _closeTo(781942785));
    expect(results[2].label, equals('MMU Overhead Memory'));
    expect(results[2].values[0], _closeTo(77529088));
    expect(results[2].values[1], _closeTo(77529089));
    expect(results[3].label, equals('IPC Memory'));
    expect(results[3].values[0], _closeTo(49152));
    expect(results[3].values[1], _closeTo(49152));
    expect(results[4].label, equals('CPU Memory Bandwidth Usage'));
    expect(results[4].values[0], _closeTo(40000000));
    expect(results[4].values[1], _closeTo(50000000));
    expect(results[5].label, equals('GPU Memory Bandwidth Usage'));
    expect(results[5].values[0], _closeTo(240000000));
    expect(results[5].values[1], _closeTo(250000000));
    expect(results[6].label, equals('Other Memory Bandwidth Usage'));
    expect(results[6].values[0], _closeTo(0));
    expect(results[6].values[1], _closeTo(190000000));
    expect(results[7].label, equals('VDEC Memory Bandwidth Usage'));
    expect(results[7].values[0], _closeTo(0));
    expect(results[7].values[1], _closeTo(0));
    expect(results[8].label, equals('VPU Memory Bandwidth Usage'));
    expect(results[8].values[0], _closeTo(140000000));
    expect(results[8].values[1], _closeTo(140000000));
    expect(results[9].label, equals('Total Memory Bandwidth Usage'));
    expect(results[9].values[0], _closeTo(420000000));
    expect(results[9].values[1], _closeTo(630000000));
    expect(results[10].label, equals('Memory Bandwidth Usage'));
    expect(results[10].values[0], _closeTo(50.00));
    expect(results[10].values[1], _closeTo(75.00));

    final resultsExcludingBandwidth =
        memoryMetricsProcessor(model, {'exclude_bandwidth': true});
    expect(resultsExcludingBandwidth.length, 4);
    expect(resultsExcludingBandwidth[0].label, equals('Total System Memory'));
    expect(resultsExcludingBandwidth[0].values[0], _closeTo(940612736));
    expect(resultsExcludingBandwidth[0].values[1], _closeTo(990612736));
    expect(resultsExcludingBandwidth[1].label, equals('VMO Memory'));
    expect(resultsExcludingBandwidth[1].values[0], _closeTo(781942784));
    expect(resultsExcludingBandwidth[1].values[1], _closeTo(781942785));
    expect(resultsExcludingBandwidth[2].label, equals('MMU Overhead Memory'));
    expect(resultsExcludingBandwidth[2].values[0], _closeTo(77529088));
    expect(resultsExcludingBandwidth[2].values[1], _closeTo(77529089));
    expect(resultsExcludingBandwidth[3].label, equals('IPC Memory'));
    expect(resultsExcludingBandwidth[3].values[0], _closeTo(49152));
    expect(resultsExcludingBandwidth[3].values[1], _closeTo(49152));
  });

  test('Power metric', () async {
    final model = await _modelFromPath('runtime_deps/power_metric.json');
    final results = powerMetricsProcessor(model, {});
    expect(results.length, 1);
    expect(results[0].label, equals('Device power'));
    expect(results[0].values[0], _closeTo(1.0));
    expect(results[0].values[1], _closeTo(3.0));

    final aggregatedResults =
        powerMetricsProcessor(model, {'aggregateMetricsOnly': true});
    expect(aggregatedResults.length, equals(8));
    expect(aggregatedResults[0].label, equals('power_p5'));
    expect(aggregatedResults[0].values[0], _closeTo(1.1));
    expect(aggregatedResults[1].label, equals('power_p25'));
    expect(aggregatedResults[1].values[0], _closeTo(1.5));
    expect(aggregatedResults[2].label, equals('power_p50'));
    expect(aggregatedResults[2].values[0], _closeTo(2.0));
    expect(aggregatedResults[3].label, equals('power_p75'));
    expect(aggregatedResults[3].values[0], _closeTo(2.5));
    expect(aggregatedResults[4].label, equals('power_p95'));
    expect(aggregatedResults[4].values[0], _closeTo(2.9));
    expect(aggregatedResults[5].label, equals('power_min'));
    expect(aggregatedResults[5].values[0], _closeTo(1.0));
    expect(aggregatedResults[6].label, equals('power_max'));
    expect(aggregatedResults[6].values[0], _closeTo(3.0));
    expect(aggregatedResults[7].label, equals('power_average'));
    expect(aggregatedResults[7].values[0], _closeTo(2.0));
  });

  test('Custom registry', () async {
    List<TestCaseResults> testProcessor(
            Model _model, Map<String, dynamic> _extraArgs) =>
        [
          TestCaseResults(
            'test',
            Unit.count,
            [
              1234,
              5678,
            ],
          ),
        ];

    final Map<String, MetricsProcessor> emptyRegistry = {};
    final Map<String, MetricsProcessor> testRegistry = {
      'test': testProcessor,
    };
    final model = Model();
    final metricsSpec = MetricsSpec(name: 'test');
    expect(() => processMetrics(model, metricsSpec, registry: emptyRegistry),
        throwsA(TypeMatcher<ArgumentError>()));
    final results = processMetrics(model, metricsSpec, registry: testRegistry);
    expect(results[0].values[0], _closeTo(1234.00));
    expect(results[0].values[1], _closeTo(5678.00));
  });

  test('Input latency metric', () async {
    final model = await _modelFromPath('runtime_deps/input_latency.json');
    final results = inputLatencyMetricsProcessor(model, {});

    expect(computeMean(results[0].values), _closeTo(77.39932275));
  });

  test('Integral timestamp and duration', () async {
    final model = createModelFromJsonString('''
{
  "displayTimeUnit": "ns",
  "traceEvents": [
    {
      "cat": "test",
      "name": "integral",
      "ts": 12345,
      "pid": 35204,
      "tid": 323993,
      "ph": "X",
      "dur": 200
    }
  ],
  "systemTraceEvents": {
    "events": [],
    "type": "fuchsia"
  }
}
''');

    expect(getAllEvents(model), isNotEmpty);
  });

  test('Flow event binding points', () async {
    final model = await _modelFromPath('runtime_deps/flow_event_binding.json');

    final thread = model.processes.single.threads.single;
    final flowEvents = filterEventsTyped<FlowEvent>(thread.events).toList();
    expect(flowEvents.length, 6);
    expect(
        flowEvents[0].enclosingDuration.start.toEpochDelta().toMillisecondsF(),
        10.0);
    expect(
        flowEvents[1].enclosingDuration.start.toEpochDelta().toMillisecondsF(),
        20.0);
    expect(
        flowEvents[2].enclosingDuration.start.toEpochDelta().toMillisecondsF(),
        40.0);
    expect(
        flowEvents[3].enclosingDuration.start.toEpochDelta().toMillisecondsF(),
        50.0);
    expect(
        flowEvents[4].enclosingDuration.start.toEpochDelta().toMillisecondsF(),
        60.0);
    expect(
        flowEvents[5].enclosingDuration.start.toEpochDelta().toMillisecondsF(),
        70.0);
  });

  test('Chrome metadata events', () async {
    final model = await _modelFromPath('runtime_deps/chrome_metadata.json');

    final process = model.processes.single;
    final thread = process.threads.single;

    expect(process.name, 'Test process');
    expect(thread.name, 'Test thread');
  });

  test('Async events with id2', () async {
    final model = await _modelFromPath('runtime_deps/id2_async.json');

    expect(model.processes.length, 2);

    expect(
        filterEventsTyped<AsyncEvent>(getAllEvents(model),
                category: 'test', name: 'async')
            .toList()
            .length,
        2);
    expect(
        filterEventsTyped<AsyncEvent>(getAllEvents(model),
                category: 'test', name: 'async2')
            .toList()
            .length,
        2);
  });

  test('Wall time metric', () async {
    final model = await _modelFromPath('runtime_deps/flutter_app.json');
    final results = totalTraceWallTimeMetricsProcessor(model, {});
    expect(results[0].values[0], _closeTo(16247.062083));
  });

  test('Flow ids', () async {
    final model = await _modelFromPath('runtime_deps/flow_ids.json');

    final events = getAllEvents(model);
    final flowEvents = filterEventsTyped<FlowEvent>(events).toList()
      ..sort((a, b) => a.start.compareTo(b.start));

    expect(flowEvents.length, 3);
    expect(flowEvents[0].nextFlow, isNotNull);
    expect(flowEvents[1].nextFlow, isNotNull);
    expect(flowEvents[2].nextFlow, isNull);

    expect(getAllEvents(model).length, 4);
  });

  test('Camera metrics', () async {
    final model = await _modelFromPath('runtime_deps/camera.json');
    final results = cameraMetricsProcessor(model, {});

    expect(results[0].values[0], _closeTo(50.55131708739538));
  });

  test('Trace slicing smoke test', () async {
    final model = _getTestModel();
    final slicedModel = model.slice(
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697800000)),
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(698600000)),
    );
    expect(getAllEvents(slicedModel).length, 2);

    final tailModel = model.slice(
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697800000)),
      null,
    );
    expect(getAllEvents(tailModel).length, 4);

    final headModel = model.slice(
      null,
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(698600000)),
    );
    expect(getAllEvents(headModel).length, 5);

    // Slicing with a doubly-infinite inteval should result in an identical
    // model.
    final copiedModel = model.slice(null, null);
    _checkModelsEqual(model, copiedModel);
  });

  test('Trace slice has no references to old model', () async {
    // Use a full-featured trace to get coverage of all the event types.
    final baseModel = await _modelFromPath('runtime_deps/flutter_app.json');
    final slicedModel = baseModel.slice(
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(1120000000)),
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(1130000000)),
    );

    expect(getAllEvents(slicedModel).length, greaterThan(0));

    // d7fece is the beginning of the output of
    //
    // echo 'OLD_MODEL_SENTINEL' | md5sum
    //
    // It has no meaning other than to make it less likely that it matches a
    // real trace event in the case that the base trace is changed.
    const sentinelName = 'OLD_MODEL_SENTINEL_d7fece';

    for (final event in getAllEvents(baseModel)) {
      event.name = sentinelName;
    }

    for (final event in getAllEvents(slicedModel)) {
      if (event is DurationEvent) {
        for (final child in event.childDurations) {
          expect(child, isNotNull);
          expect(child.name, isNot(sentinelName));
        }
        for (final flow in event.childFlows) {
          expect(flow, isNotNull);
          expect(flow.name, isNot(sentinelName));
        }
      } else if (event is FlowEvent) {
        expect(event.previousFlow?.name, isNot(sentinelName));
        expect(event.nextFlow?.name, isNot(sentinelName));
      }
    }
  });

  test('Sliced trace FPS metric', () async {
    final baseModel = await _modelFromPath('runtime_deps/flutter_app.json');
    final slicedModel = baseModel.slice(
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(1114600000)),
      TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(1130000000)),
    );
    final results =
        drmFpsMetricsProcessor(slicedModel, {'flutterAppName': 'flutter_app'});
    expect(computeMean(results[0].values), _closeTo(57.67083287297894));
    expect(results[1].values[0], _closeTo(59.95398508263344));
    expect(results[2].values[0], _closeTo(59.9981988540704));
    expect(results[3].values[0], _closeTo(60.03445978045461));
  });

  test('Memory metric missing fixed', () async {
    // One useless event with memory_monitor category so that the check that
    // memory_monitor data is there passes.
    final event = CounterEvent(
        1,
        'memory_monitor',
        'useless',
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(1000)),
        1234,
        1235, {});
    final thread = Thread(1235, events: [event]);
    final process = Process(1234, threads: [thread]);
    final model = Model()..processes = [process];

    final memoryMetrics = memoryMetricsProcessor(model, {});
    expect(memoryMetrics, equals([]));
  });
}
