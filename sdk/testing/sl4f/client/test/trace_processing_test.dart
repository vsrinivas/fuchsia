// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:sl4f/trace_processing.dart';

const String _testModelJsonString = '''
{
  "displayTimeUnit": "ns",
  "traceEvents": [
    {
      "cat": "io",
      "name": "AsyncReadWrite",
      "ts": 697503138,
      "id": 43,
      "pid": 7009,
      "tid": 7022,
      "ph": "b"
    },
    {
      "cat": "io",
      "name": "Read",
      "ts": 697503138.9531089,
      "pid": 7009,
      "tid": 7021,
      "ph": "B"
    },
    {
      "cat": "io",
      "name": "ReadWriteFlow",
      "ts": 697503139.9531089,
      "pid": 7009,
      "tid": 7021,
      "ph": "s",
      "id": 0
    },
    {
      "cat": "io",
      "name": "Write",
      "ts": 697778328.2160872,
      "pid": 7009,
      "tid": 7022,
      "ph": "B"
    },
    {
      "cat": "io",
      "name": "ReadWriteFlow",
      "ts": 697779328.2160872,
      "pid": 7009,
      "tid": 7022,
      "ph": "t",
      "id": 0
    },
    {
      "cat": "io",
      "name": "Read",
      "ts": 697868185.3588456,
      "pid": 7010,
      "tid": 7023,
      "ph": "B"
    },
    {
      "cat": "io",
      "name": "ReadWriteFlow",
      "ts": 697868050.2160872,
      "pid": 7009,
      "tid": 7022,
      "ph": "f",
      "bp": "e",
      "id": 0
    },
    {
      "cat": "io",
      "name": "Read",
      "ts": 697868571.6018075,
      "pid": 7010,
      "tid": 7023,
      "ph": "E"
    },
    {
      "cat": "io",
      "name": "Write",
      "ts": 697868582.5994568,
      "pid": 7009,
      "tid": 7022,
      "ph": "E"
    },
    {
      "cat": "io",
      "name": "AsyncReadWrite",
      "ts": 698607461,
      "id": 43,
      "pid": 7009,
      "tid": 7022,
      "ph": "e"
    },
    {
      "cat": "io",
      "name": "Read",
      "ts": 698607461.7395687,
      "pid": 7009,
      "tid": 7021,
      "ph": "E"
    },
    {
      "cat": "log",
      "name": "log",
      "ph": "i",
      "ts": 698607465.312,
      "pid": 7009,
      "tid": 7021,
      "s": "g",
      "args": {
        "message": "[INFO:trace_manager.cc(66)] Stopping trace"
      }
    },
    {
      "cat": "system_metrics",
      "name": "cpu_usage",
      "ts": 698607465.375,
      "pid": 7010,
      "tid": 7023,
      "ph": "C",
      "args": {
        "average_cpu_percentage": 0.89349317793,
        "max_cpu_usage": 0.1234
      }
    }
  ],
  "systemTraceEvents": {
    "type": "fuchsia",
    "events": [
      {
        "ph": "p",
        "pid": 7009,
        "name": "root_presenter"
      },
      {
        "ph": "t",
        "pid": 7009,
        "tid": 7022,
        "name": "initial-thread"
      }
    ]
  }
}
''';

Model _getTestModel() {
  final readEvent = DurationEvent()
    ..category = 'io'
    ..name = 'Read'
    ..pid = 7009
    ..tid = 7021
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697503138.9531089))
    ..duration = TimeDelta.fromMicroseconds(698607461.7395687) -
        TimeDelta.fromMicroseconds(697503138.9531089);

  final writeEvent = DurationEvent()
    ..category = 'io'
    ..name = 'Write'
    ..pid = 7009
    ..tid = 7022
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697778328.2160872))
    ..duration = TimeDelta.fromMicroseconds(697868582.5994568) -
        TimeDelta.fromMicroseconds(697778328.2160872);

  final asyncReadWriteEvent = AsyncEvent()
    ..category = 'io'
    ..name = 'AsyncReadWrite'
    ..pid = 7009
    ..tid = 7022
    ..start = TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697503138))
    ..id = 43
    ..duration = TimeDelta.fromMicroseconds(698607461.0) -
        TimeDelta.fromMicroseconds(697503138.0);

  final readEvent2 = DurationEvent()
    ..category = 'io'
    ..name = 'Read'
    ..pid = 7010
    ..tid = 7023
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697868185.3588456))
    ..duration = TimeDelta.fromMicroseconds(697868571.6018075) -
        TimeDelta.fromMicroseconds(697868185.3588456);

  final flowStart = FlowEvent()
    ..category = 'io'
    ..name = 'ReadWriteFlow'
    ..pid = 7009
    ..tid = 7021
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697503139.9531089))
    ..id = 0
    ..phase = FlowEventPhase.start;

  final flowStep = FlowEvent()
    ..category = 'io'
    ..name = 'ReadWriteFlow'
    ..pid = 7009
    ..tid = 7022
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697779328.2160872))
    ..id = 0
    ..phase = FlowEventPhase.step;

  final flowEnd = FlowEvent()
    ..category = 'io'
    ..name = 'ReadWriteFlow'
    ..pid = 7009
    ..tid = 7022
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(697868050.2160872))
    ..id = 0
    ..phase = FlowEventPhase.end;

  final counterEvent = CounterEvent()
    ..category = 'system_metrics'
    ..name = 'cpu_usage'
    ..pid = 7010
    ..tid = 7023
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(698607465.375))
    ..args = {'average_cpu_percentage': 0.89349317793, 'max_cpu_usage': 0.1234};

  final instantEvent = InstantEvent()
    ..category = 'log'
    ..name = 'log'
    ..pid = 7009
    ..tid = 7021
    ..start =
        TimePoint.fromEpochDelta(TimeDelta.fromMicroseconds(698607465.312))
    ..scope = InstantEventScope.global
    ..args = {'message': '[INFO:trace_manager.cc(66)] Stopping trace'};

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

  final thread7021 = Thread()
    ..tid = 7021
    ..name = ''
    ..events = [readEvent, flowStart, instantEvent];

  final thread7022 = Thread()
    ..tid = 7022
    ..name = 'initial-thread'
    ..events = [asyncReadWriteEvent, writeEvent, flowStep, flowEnd];

  final thread7023 = Thread()
    ..tid = 7023
    ..name = ''
    ..events = [readEvent2, counterEvent];

  final process7009 = Process()
    ..pid = 7009
    ..name = 'root_presenter'
    ..threads = [thread7021, thread7022];

  final process7010 = Process()
    ..pid = 7010
    ..name = ''
    ..threads = [thread7023];

  final model = Model()..processes = [process7009, process7010];

  return model;
}

Map<String, dynamic> _toDictionary(Event e) {
  final result = {
    'category': e.category,
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
    result['id'] = e.id;
  } else if (e is DurationEvent) {
    result['duration.toNanoseconds()'] = e.duration.toNanoseconds();
    result['!!parent'] = e.parent != null;
    result['childDurations.length'] = e.childDurations.length;
    result['childFlows.length'] = e.childFlows.length;
  } else if (e is AsyncEvent) {
    result['id'] = e.id;
    result['duration'] = e.duration;
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
    result &= a.duration == b.duration;
    result &= (a.parent == null) == (b.parent == null);
    result &= a.childDurations.length == b.childDurations.length;
    result &= a.childFlows.length == b.childFlows.length;
  } else if (a is AsyncEvent && b is AsyncEvent) {
    result &= a.id == b.id;
    result &= a.duration == b.duration;
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

void main(List<String> args) {
  test('Create trace model', () async {
    final testModel = _getTestModel();
    final testModelFromJsonString =
        createModelFromJsonString(_testModelJsonString);
    _checkModelsEqual(testModel, testModelFromJsonString);
  });
}
