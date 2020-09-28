// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'time_delta.dart';
import 'time_point.dart';
import 'trace_model.dart';

/// Create a [Model] from a file path.
Future<Model> createModelFromFilePath(String path) async =>
    createModelFromFile(File(path));

/// Create a [Model] from a [File].
Future<Model> createModelFromFile(File file) async =>
    createModelFromJsonString(await file.readAsString());

/// Create a [Model] from a raw JSON string of trace data.
Model createModelFromJsonString(String jsonString) {
  final jsonObject = json.decode(jsonString);
  return createModelFromJson(jsonObject);
}

/// Create a [Model] from a root raw JSON trace object.
Model createModelFromJson(Map<String, dynamic> rootObject) =>
    _createModelFromJson(rootObject);

/// A helper function to load common fields into an [Event] from a raw JSON trace
/// event.
void _fromJsonCommon(Event event, Map<String, dynamic> jsonTraceEvent) {
  event
    ..category = jsonTraceEvent['cat']
    ..name = jsonTraceEvent['name']
    ..pid = jsonTraceEvent['pid']
    ..tid = jsonTraceEvent['tid'].toInt()
    ..start = TimePoint.fromEpochDelta(
        TimeDelta.fromMicroseconds(jsonTraceEvent['ts']));

  final args = jsonTraceEvent['args'];
  if (args != null) {
    event.args = args;
  }
}

/// Assert that expected fields in a JSON trace event are present and are of
/// the correct type.  If any of these fields are missing or is of a different
/// type than what is asserted here, then the JSON trace event is considered to
/// be malformed.
void _checkTraceEvent(Map<String, dynamic> jsonTraceEvent) {
  if (jsonTraceEvent['ph'] != 'M' &&
      !(jsonTraceEvent.containsKey('cat') && jsonTraceEvent['cat'] is String)) {
    throw FormatException(
        'Expected $jsonTraceEvent to have field "cat" of type String');
  }
  if (!(jsonTraceEvent.containsKey('name') &&
      jsonTraceEvent['name'] is String)) {
    throw FormatException(
        'Expected $jsonTraceEvent to have field "name" of type String');
  }
  if (jsonTraceEvent['ph'] != 'M' &&
      !(jsonTraceEvent.containsKey('ts') &&
          (jsonTraceEvent['ts'] is double || jsonTraceEvent['ts'] is int))) {
    throw FormatException(
        'Expected $jsonTraceEvent to have field "ts" of type double or int');
  }
  if (!(jsonTraceEvent.containsKey('pid') && jsonTraceEvent['pid'] is int)) {
    throw FormatException(
        'Expected $jsonTraceEvent to have field "pid" of type int');
  }
  if (!(jsonTraceEvent.containsKey('tid') &&
      (jsonTraceEvent['tid'] is double || jsonTraceEvent['tid'] is int))) {
    throw FormatException(
        'Expected $jsonTraceEvent to have field "tid" of type double or int');
  }

  // It's legal for "args" to not be a key in the object.
  if (jsonTraceEvent.containsKey('args')) {
    if (!(jsonTraceEvent['args'] is Map<String, dynamic>)) {
      throw FormatException(
          'Expected $jsonTraceEvent with "args" field to have "args" field of type Map<String, dynamic>');
    }
  }
}

/// A helper class to group events by their "track" ((pid, tid) pairs).
class _TrackKey {
  int pid;
  int tid;

  _TrackKey(this.pid, this.tid);

  @override
  bool operator ==(Object other) {
    if (other is! _TrackKey) {
      return false;
    }
    _TrackKey tracekKey = other;
    return pid == tracekKey.pid && tid == tracekKey.tid;
  }

  @override
  int get hashCode {
    // Hash combine logic inspired by https://dart.dev/guides/libraries/library-tour#implementing-map-keys.
    int result = 17;
    result = 37 * result + pid.hashCode;
    result = 37 * result + tid.hashCode;
    return result;
  }
}

/// A helper struct to group flow events.
class _FlowKey {
  String category;
  String name;
  int id;
  // Only used for 'local' flow ids.
  int pid;

  _FlowKey.fromTraceEvent(Map<String, dynamic> traceEvent) {
    name = traceEvent['name'];
    category = traceEvent['cat'];

    // Unlike _AsyncKey, _FlowKey is globally scoped unless specifically local.
    pid = 0;

    if (traceEvent.containsKey('id')) {
      if (traceEvent['id'] is int) {
        id = traceEvent['id'];
      } else if (traceEvent['id'] is String) {
        id = int.tryParse(traceEvent['id']);
      }
    } else if (traceEvent.containsKey('id2')) {
      final Map<String, dynamic> id2 = traceEvent['id2'];
      if (id2.containsKey('local')) {
        // 'local' id2 means scoped to the process.
        pid = traceEvent['pid'];
        if (id2['local'] is int) {
          id = id2['local'];
        } else if (id2['local'] is String) {
          id = int.tryParse(id2['local']);
        }
      } else if (id2.containsKey('global')) {
        if (id2['global'] is int) {
          id = id2['global'];
        } else if (id2['global'] is String) {
          id = int.tryParse(id2['global']);
        }
      }
    }

    if (id == null) {
      throw FormatException('Could not find id in $traceEvent');
    }
  }

  @override
  bool operator ==(Object other) {
    if (other is! _FlowKey) {
      return false;
    }
    _FlowKey flowKey = other;
    return category == flowKey.category &&
        name == flowKey.name &&
        id == flowKey.id &&
        pid == flowKey.pid;
  }

  @override
  int get hashCode {
    // Hash combine logic inspired by https://dart.dev/guides/libraries/library-tour#implementing-map-keys.
    int result = 17;
    result = 37 * result + category.hashCode;
    result = 37 * result + name.hashCode;
    result = 37 * result + id.hashCode;
    result = 37 * result + pid.hashCode;
    return result;
  }
}

/// A helper struct to group async events.
class _AsyncKey {
  int pid;
  String category;
  String name;
  int id;

  _AsyncKey.fromTraceEvent(Map<String, dynamic> traceEvent) {
    pid = traceEvent['pid'];
    name = traceEvent['name'];
    category = traceEvent['cat'];

    if (traceEvent.containsKey('id')) {
      if (traceEvent['id'] is int) {
        id = traceEvent['id'];
      } else if (traceEvent['id'] is String) {
        id = int.tryParse(traceEvent['id']);
      }
    } else if (traceEvent.containsKey('id2')) {
      final Map<String, dynamic> id2 = traceEvent['id2'];
      if (id2.containsKey('local')) {
        // 'local' id2 means scoped to the process.
        if (id2['local'] is int) {
          id = id2['local'];
        } else if (id2['local'] is String) {
          id = int.tryParse(id2['local']);
        }
      } else if (id2.containsKey('global')) {
        pid = 0;
        if (id2['global'] is int) {
          id = id2['global'];
        } else if (id2['global'] is String) {
          id = int.tryParse(id2['global']);
        }
      }
    }

    if (id == null) {
      throw FormatException('Could not find id in $traceEvent');
    }
  }

  @override
  bool operator ==(Object other) {
    if (other is! _AsyncKey) {
      return false;
    }
    _AsyncKey asyncKey = other;
    return pid == asyncKey.pid &&
        category == asyncKey.category &&
        name == asyncKey.name &&
        id == asyncKey.id;
  }

  @override
  int get hashCode {
    // Hash combine logic inspired by https://dart.dev/guides/libraries/library-tour#implementing-map-keys.
    int result = 17;
    result = 37 * result + pid.hashCode;
    result = 37 * result + category.hashCode;
    result = 37 * result + name.hashCode;
    result = 37 * result + id.hashCode;
    return result;
  }
}

Map<String, dynamic> _combineArgs(
    Map<String, dynamic> args1, Map<String, dynamic> args2) {
  if (args1 == null) {
    return args2;
  } else if (args2 == null) {
    return args1;
  }
  return {...args1, ...args2};
}

Model _createModelFromJson(Map<String, dynamic> rootObject) {
  final resultEvents = <Event>[];

  // Maintains the current duration stack for each track.
  final Map<_TrackKey, List<DurationEvent>> durationStacks = {};
  // Maintains in progress async events.
  final Map<_AsyncKey, AsyncEvent> liveAsyncEvents = {};
  // Maintains in progress flow sequences.
  final Map<_FlowKey, FlowEvent> liveFlows = {};
  // Flows with "next slide" binding that are waiting for to be bound.
  final Map<_TrackKey, List<FlowEvent>> unboundFlowEvents = {};

  // A helper lambda to add duration events to the appropriate duration stack
  // and do the appropriate duration/flow graph setup.  It is used for both
  // begin/end pairs and complete events.
  void addToDurationStack(
      DurationEvent durationEvent, List<DurationEvent> durationStack) {
    durationStack.add(durationEvent);
    if (durationStack.length > 1) {
      final top = durationStack.last;
      final topParent = durationStack[durationStack.length - 2];
      top.parent = topParent;
      topParent.childDurations.add(durationEvent);
    }
  }

  if (!(rootObject.containsKey('traceEvents') &&
      rootObject['traceEvents'] is List<dynamic>)) {
    throw FormatException(
        'Expected $rootObject to have field "traceEvents" of type List<dynamic>');
  }
  final List<dynamic> traceEvents = rootObject['traceEvents'];

  // Add synthetic end events for each complete event in the trace data to
  // assist with maintaining each thread's duration stack.  This isn't
  // strictly necessary, however it makes the duration stack bookkeeping simpler.
  final extendedTraceEvents = List<Map<String, dynamic>>.from(traceEvents);
  for (final traceEvent in traceEvents) {
    if (traceEvent['ph'] == 'X') {
      final syntheticEndEvent = Map<String, dynamic>.from(traceEvent);
      syntheticEndEvent['ph'] = 'fuchsia_synthetic_end';
      syntheticEndEvent['ts'] = traceEvent['ts'] + traceEvent['dur'];
      extendedTraceEvents.add(syntheticEndEvent);
    }
  }

  // Sort the events by their timestamp.  We need to iterate through the
  // events in sorted order to compute things such as duration stacks and flow
  // sequences. Events without timestamps (e.g. Chrome's metadata events) are
  // sorted to the beginning.
  extendedTraceEvents.sort((a, b) => (a['ts'] ?? 0).compareTo(b['ts'] ?? 0));

  int droppedFlowEventCounter = 0;
  int droppedAsyncEventCounter = 0;

  // TODO(fxbug.dev/41309): Support nested async events.  In the meantime, just drop them.
  int droppedNestedAsyncEventCounter = 0;

  final Map<int, String> pidToName = {};
  final Map<int, String> tidToName = {};

  // Process the raw trace events into our model's [Event] representation.
  for (final traceEvent in extendedTraceEvents) {
    _checkTraceEvent(traceEvent);
    final int pid = traceEvent['pid'];
    final int tid = traceEvent['tid'].toInt();

    final trackKey = _TrackKey(pid, tid);
    final durationStack =
        durationStacks.putIfAbsent(trackKey, () => <DurationEvent>[]);

    final phase = traceEvent['ph'];
    if (phase == 'X') {
      final durationEvent = DurationEvent();
      _fromJsonCommon(durationEvent, traceEvent);
      if (!(traceEvent.containsKey('dur') &&
          (traceEvent['dur'] is double || traceEvent['dur'] is int))) {
        throw FormatException(
            'Expected $traceEvent to have field "dur" of type double or int');
      }
      durationEvent.duration = TimeDelta.fromMicroseconds(traceEvent['dur']);

      if (unboundFlowEvents.containsKey(trackKey)) {
        for (final flowEvent in unboundFlowEvents[trackKey]) {
          flowEvent.enclosingDuration = durationEvent;
        }
        unboundFlowEvents[trackKey].clear();
      }
      addToDurationStack(durationEvent, durationStack);
      resultEvents.add(durationEvent);
    } else if (phase == 'B') {
      final durationEvent = DurationEvent();
      _fromJsonCommon(durationEvent, traceEvent);

      if (unboundFlowEvents.containsKey(trackKey)) {
        for (final flowEvent in unboundFlowEvents[trackKey]) {
          flowEvent.enclosingDuration = durationEvent;
        }
        unboundFlowEvents[trackKey].clear();
      }
      addToDurationStack(durationEvent, durationStack);
    } else if (phase == 'E') {
      if (durationStack.isNotEmpty) {
        final popped = durationStack.removeLast();
        popped.duration ??= TimePoint.fromEpochDelta(
                TimeDelta.fromMicroseconds(traceEvent['ts'])) -
            popped.start;
        popped.args = _combineArgs(popped.args, traceEvent['args']);
        resultEvents.add(popped);
      }
    } else if (phase == 'fuchsia_synthetic_end') {
      assert(durationStack.isNotEmpty);
      durationStack.removeLast();
    } else if (phase == 'b') {
      final asyncKey = _AsyncKey.fromTraceEvent(traceEvent);
      final asyncEvent = AsyncEvent();
      _fromJsonCommon(asyncEvent, traceEvent);
      asyncEvent.id = asyncKey.id;
      liveAsyncEvents[asyncKey] = asyncEvent;
    } else if (phase == 'e') {
      final asyncKey = _AsyncKey.fromTraceEvent(traceEvent);
      final asyncEvent = liveAsyncEvents.remove(asyncKey);
      if (asyncEvent != null) {
        asyncEvent
          ..duration = TimePoint.fromEpochDelta(
                  TimeDelta.fromMicroseconds(traceEvent['ts'])) -
              asyncEvent.start
          ..args = _combineArgs(asyncEvent.args, traceEvent['args']);
      } else {
        droppedAsyncEventCounter++;
        continue;
      }
      resultEvents.add(asyncEvent);
    } else if (phase == 'i' || phase == 'I') {
      if (!(traceEvent.containsKey('s') && traceEvent['s'] is String)) {
        throw FormatException(
            'Expected $traceEvent to have field "s" of type String');
      }
      final String scope = traceEvent['s'];
      if (!(scope == 'g' || scope == 'p' || scope == 't')) {
        throw FormatException(
            'Expected "s" (scope) field of $traceEvent to have value in {"g", "p", "t"}');
      }
      final instantEvent = InstantEvent();
      _fromJsonCommon(instantEvent, traceEvent);
      instantEvent.scope = {
        'g': InstantEventScope.global,
        'p': InstantEventScope.process,
        't': InstantEventScope.thread
      }[scope];

      resultEvents.add(instantEvent);
    } else if (phase == 's' || phase == 't' || phase == 'f') {
      String bindingPoint;

      if (traceEvent.containsKey('bp')) {
        if (traceEvent['bp'] == 'e') {
          bindingPoint = 'enclosing';
        } else {
          throw FormatException('Found unexpected value in bp field of '
              '$traceEvent');
        }
      } else if (phase == 's' || phase == 't') {
        bindingPoint = 'enclosing';
      } else if (phase == 'f') {
        bindingPoint = 'next';
      }

      final flowKey = _FlowKey.fromTraceEvent(traceEvent);

      FlowEvent previousFlow;
      if (phase == 's') {
        if (liveFlows.containsKey(flowKey)) {
          droppedFlowEventCounter++;
          continue;
        }
      } else if (phase == 't' || phase == 'f') {
        previousFlow = liveFlows[flowKey];
        if (previousFlow == null) {
          droppedFlowEventCounter++;
          continue;
        }
      }

      if (durationStack.isEmpty) {
        droppedFlowEventCounter++;
        continue;
      }
      final enclosingDuration =
          bindingPoint == 'enclosing' ? durationStack.last : null;

      final flowEvent = FlowEvent();
      _fromJsonCommon(flowEvent, traceEvent);
      flowEvent
        ..id = flowKey.id
        ..phase = {
          's': FlowEventPhase.start,
          't': FlowEventPhase.step,
          'f': FlowEventPhase.end
        }[phase]
        ..enclosingDuration = enclosingDuration;
      if (bindingPoint == 'enclosing') {
        enclosingDuration.childFlows.add(flowEvent);
      } else {
        unboundFlowEvents
            .putIfAbsent(trackKey, () => <FlowEvent>[])
            .add(flowEvent);
      }

      if (previousFlow != null) {
        previousFlow.nextFlow = flowEvent;
      }
      flowEvent.previousFlow = previousFlow;

      if (phase == 's' || phase == 't') {
        liveFlows[flowKey] = flowEvent;
      } else {
        liveFlows.remove(flowKey);
      }
      resultEvents.add(flowEvent);
    } else if (phase == 'C') {
      int id;
      if (traceEvent.containsKey('id')) {
        if (traceEvent['id'] is int) {
          id = traceEvent['id'];
        } else if (traceEvent['id'] is String) {
          id = int.tryParse(traceEvent['id']);
        }
        if (id == null) {
          throw FormatException(
              'Expected $traceEvent with "id" field set to be of type int '
              'or a string that parses as an int');
        }
      }
      final counterEvent = CounterEvent();
      _fromJsonCommon(counterEvent, traceEvent);
      counterEvent.id = id;
      resultEvents.add(counterEvent);
    } else if (phase == 'n') {
      // TODO(fxbug.dev/41309): Support nested async events.  In the meantime, just drop them.
      droppedNestedAsyncEventCounter++;
    } else if (phase == 'M') {
      // Chrome metadata events. These define process and thread names, similar
      // to the Fuchsia systemTraceEvents.
      if (traceEvent['name'] == 'process_name') {
        // If traceEvent contains args, those are verified to be of type
        // Map<String, dynamic> in _checkTraceEvent.
        if (!traceEvent.containsKey('args') ||
            !traceEvent['args'].containsKey('name')) {
          throw FormatException('$traceEvent is a process_name metadata event'
              'but doesn\'t have a name argument');
        }
        pidToName[pid] = traceEvent['args']['name'];
      }
      if (traceEvent['name'] == 'thread_name') {
        // If traceEvent contains args, those are verified to be of type
        // Map<String, dynamic> in _checkTraceEvent.
        if (!traceEvent.containsKey('args') ||
            !traceEvent['args'].containsKey('name')) {
          throw FormatException('$traceEvent is a thread_name metadata event'
              'but doesn\'t have a name argument');
        }
        tidToName[tid] = traceEvent['args']['name'];
      }
    } else if (phase == 'R' ||
        phase == '(' ||
        phase == ')' ||
        phase == 'O' ||
        phase == 'N' ||
        phase == 'D' ||
        phase == 'S' ||
        phase == 'T' ||
        phase == 'p' ||
        phase == 'F') {
      // Ignore some phases that are in Chrome traces that we don't yet have use
      // cases for.
      //
      // These are:
      // * 'R' - Mark events, similar to instants created by the Navigation
      //         Timing API
      // * '(', ')' - Context events
      // * 'O', 'N', 'D' - Object events
      // * 'S', 'T', 'p', 'F' - Legacy async events
    } else {
      throw FormatException(
          'Encountered unknown phase $phase from $traceEvent');
    }
  }
  final liveDurationEventsCount =
      durationStacks.values.map((ds) => ds.length).fold(0, (a, b) => a + b);
  if (liveDurationEventsCount > 0) {
    print(
        'Warning, finished processing trace events with $liveDurationEventsCount in progress duration events');
  }
  if (liveAsyncEvents.isNotEmpty) {
    print(
        'Warning, finished processing trace events with ${liveAsyncEvents.length} in progress async events');
  }
  if (liveFlows.isNotEmpty) {
    print(
        'Warning, finished processing trace events with ${liveFlows.length} in progress flow events');
  }
  if (droppedAsyncEventCounter > 0) {
    print('Warning, dropped $droppedAsyncEventCounter async events');
  }
  if (droppedFlowEventCounter > 0) {
    print('Warning, dropped $droppedFlowEventCounter flow events');
  }
  if (droppedNestedAsyncEventCounter > 0) {
    print(
        'Warning, dropped $droppedNestedAsyncEventCounter nested async events');
  }

  if (rootObject.containsKey('systemTraceEvents')) {
    if (!(rootObject['systemTraceEvents'] is Map<String, dynamic>)) {
      throw FormatException('Expected field "systemTraceEvents" to be of type '
          'Map<String, dynamic>');
    }
    final systemTraceEvents = rootObject['systemTraceEvents'];

    if (!(systemTraceEvents.containsKey('type') &&
        systemTraceEvents['type'] is String)) {
      throw FormatException(
          'Expected $systemTraceEvents to have field "type" of type String');
    }
    if (systemTraceEvents['type'] != 'fuchsia') {
      throw FormatException(
          'Expected $systemTraceEvents to have field "type" equal to value "fuchsia"');
    }
    if (!(systemTraceEvents.containsKey('events') &&
        systemTraceEvents['events'] is List<dynamic>)) {
      throw FormatException(
          'Expected $systemTraceEvents to have field "events" of type '
          'List<dynamic>');
    }
    final systemTraceEventsEvents = systemTraceEvents['events'];

    for (final systemTraceEvent in systemTraceEventsEvents) {
      if (!(systemTraceEvent.containsKey('ph') &&
          systemTraceEvent['ph'] is String)) {
        throw FormatException(
            'Expected $systemTraceEvent to have field "ph" of type String');
      }
      final String phase = systemTraceEvent['ph'];
      if (phase == 'p') {
        if (!(systemTraceEvent.containsKey('pid') &&
            systemTraceEvent['pid'] is int)) {
          throw FormatException(
              'Expected $systemTraceEvent to have field "pid" of type int');
        }
        final int pid = systemTraceEvent['pid'];
        if (!(systemTraceEvent.containsKey('name') &&
            systemTraceEvent['name'] is String)) {
          throw FormatException(
              'Expected $systemTraceEvent to have field "name" of type String');
        }
        final String name = systemTraceEvent['name'];
        pidToName[pid] = name;
      } else if (phase == 't') {
        if (!(systemTraceEvent.containsKey('pid') &&
            systemTraceEvent['pid'] is int)) {
          throw FormatException(
              'Expected $systemTraceEvent to have field "pid" of type int');
        }
        if (!(systemTraceEvent.containsKey('tid') &&
                (systemTraceEvent['tid'] is int) ||
            systemTraceEvent['tid'] is double)) {
          throw FormatException(
              'Expected $systemTraceEvent to have field "tid" of type int or double');
        }
        final int tid = systemTraceEvent['tid'].toInt();
        if (!(systemTraceEvent.containsKey('name') &&
            systemTraceEvent['name'] is String)) {
          throw FormatException(
              'Expected $systemTraceEvent to have field "name" of type String');
        }
        final String name = systemTraceEvent['name'];
        tidToName[tid] = name;
      } else if (phase == 'k') {
        // CPU events are currently ignored.  It would be interesting to support
        // these in the future so we can track CPU durations in addition to wall
        // durations.
      } else {
        print('Unknown phase $phase from $systemTraceEvent');
      }
    }
  }

  resultEvents.sort((a, b) => a.start.compareTo(b.start));

  final Map<int, Process> processes = {};
  for (final event in resultEvents) {
    final process =
        processes.putIfAbsent(event.pid, () => Process()..pid = event.pid);

    int threadIndex = process.threads.indexWhere((e) => e.tid == event.tid);
    if (threadIndex == -1) {
      final thread = Thread()..tid = event.tid;
      if (tidToName.containsKey(event.tid)) {
        thread.name = tidToName[event.tid];
      }
      process.threads.add(thread);
      threadIndex = process.threads.length - 1;
    }
    final thread = process.threads[threadIndex];
    thread.events.add(event);
  }

  final model = Model();
  for (final pid in processes.keys.toList()..sort()) {
    final process = processes[pid];
    process.threads.sort((a, b) {
      return a.tid.compareTo(b.tid);
    });
    if (pidToName.containsKey(process.pid)) {
      process.name = pidToName[process.pid];
    }
    model.processes.add(process);
  }

  return model;
}
