// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'time_delta.dart';
import 'time_point.dart';

/// The root of the trace model.
class Model {
  List<Process> processes = [];
}

/// Represents a Process in the trace model.
class Process {
  /// The process id of this [Process].
  int pid;

  /// The process name of this [Process].
  String name = '';

  /// A list of [Thread]s that belong to this [Process].
  List<Thread> threads = [];
}

/// Represents a Thread in the trace model.
class Thread {
  /// The thread if of this [Thread].
  int tid;

  /// The thread name of this [Thread].
  String name = '';

  /// A list of [Event]s that belong to this [Thread].
  List<Event> events = [];
}

/// A base class for all trace events in the trace model.  Contains
/// fields that are common to all trace event types.
abstract class Event {
  /// The category of this [Event].
  String category;

  /// The name of this [Event].
  String name;

  /// The timestamp this [Event] started at.
  TimePoint start;

  /// The process id that this [Event] belongs to.
  int pid;

  /// The thread id that this [Event] belongs to.
  int tid;

  /// Extra arguments that the [Event] contains.
  Map<String, dynamic> args = {};
}

/// Represents all different scopes for [InstantEvent]s.
enum InstantEventScope { thread, process, global }

/// Represents an instant event, which is an event that corresponds to a single
/// moment in time.
class InstantEvent extends Event {
  InstantEventScope scope;
}

/// Represents a counter event.
class CounterEvent extends Event {
  /// Note that [id] is nullable for [CounterEvent]s.
  int id;
}

/// Represents a duration event.  Durations describe work which is happening
/// synchronously on one thread.  In our trace model, matching begin/end duration
/// in the raw Chrome trace format are merged into a single [DurationEvent].
/// Chrome complete events become [DurationEvent]s as well. Dangling Chrome
/// begin/end events (i.e. they don't have a matching end/begin event) are
/// dropped.
class DurationEvent extends Event {
  /// The duration of the [DurationEvent].  The "dur" field for complete events,
  /// and end['ts'] - begin['ts'] for begin/end pairs.
  TimeDelta duration;

  /// The [DurationEvent]'s parent [DurationEvent] in the duration stack.  [null]
  /// if the event has no parent event (i.e. it is the base of the duration
  /// stack).
  DurationEvent parent;

  /// A list of [DurationEvent]s that are children of this [DurationEvent].
  /// I.e., this event is their parent.
  List<DurationEvent> childDurations = [];

  /// A list of [FlowEvent]s that have this [DurationEvent] as their enclosing
  /// duration.
  List<FlowEvent> childFlows = [];
}

/// Represents an async event.  Asynchronous events describe work which is
/// happening asynchronously and which may span multiple threads.  Dangling
/// Chrome async begin/end events are dropped.
class AsyncEvent extends Event {
  int id;

  /// The duration of the async begin/end pair.
  TimeDelta duration;
}

/// Represents all different phases of flow events.
enum FlowEventPhase { start, step, end }

/// Represents a flow event. Flow events describe control flow handoffs between
/// threads or across processes.
///
/// Malformed flow events are dropped.  Malformed flow events could be any of:
///   * A begin flow event with a (category, name, id) tuple already in progress
///     (this is uncommon in practice).
///   * A step flow event with no preceding (category, name, id) tuple.
///   * An end flow event with no preceding (category, name, id) tuple.
class FlowEvent extends Event {
  String id;

  /// The phase of the [FlowEvent].
  FlowEventPhase phase;

  /// The enclosing duration that the [FlowEvent] belongs to.  This field will
  /// never be null, as [FlowEvent]s without enclosing durations are considered
  /// to be malformed.
  ///
  /// In the case of a Chrome trace, this field stores the "bound"
  /// [DurationEvent], which may be either the enclosing duration or the next
  /// duration, depending on the defined binding point.
  DurationEvent enclosingDuration;

  /// The previous flow event in the flow sequence.  Will be null for begin flow
  /// events, and will never be null for step and end flow events.
  FlowEvent previousFlow;

  /// The next flow event in the flow sequence.  Will never be null for begin and
  /// step flow events, and will be null for end flow events.
  FlowEvent nextFlow;
}
