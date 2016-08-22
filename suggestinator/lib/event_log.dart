// Copyright 2016 The Chromium Authors. All rights reserved.

import 'dart:convert';

import 'suggestion.dart';

enum EventType { SUGGESTIONS_REMOVED, SUGGESTIONS_ADDED, SUGGESTION_SELECTED, }

typedef void EventLogChangeCallback(final Event event);

class EventLog {
  static EventLog _events;

  final List<Event> _log = <Event>[];
  final List<EventLogChangeCallback> _observers = <EventLogChangeCallback>[];

  factory EventLog() {
    if (_events == null) _events = new EventLog._internal();
    return _events;
  }

  EventLog._internal();

  void addObserver(EventLogChangeCallback callback) {
    _observers.add(callback);
  }

  void removeObserver(EventLogChangeCallback callback) {
    _observers.remove(callback);
  }

  void _notifyObservers(final Event event) {
    for (EventLogChangeCallback observer in _observers) {
      observer(event);
    }
  }

  /// Publishes [event] to the log.
  void capture(final Event event) {
    if (event.body.isEmpty) return;
    _log.add(event);
    _notifyObservers(event);
  }

  Map<String, dynamic> toInspectorJson() =>
      {'type': 'event-log', 'events': _log.reversed};

  Map<String, dynamic> toJson() {
    final Map<int, Event> logMap = _log.asMap();
    return new Map.fromIterables(
        logMap.keys.map((int k) => k.toString()), logMap.values);
  }

  String toJsonString() =>
      JSON.encode(toJson(), toEncodable: (o) => o.toString());

  String toString() => toJsonString();
}

/// Encodes important changes and moments in Modular.
///
/// [Event]s will have the following JSON form (the example below is for a
/// SUGGESTIONS_REMOVED event):
///
/// {
///   'type': EventType.SUGGESTIONS_REMOVED,
///   'time': '2016-07-19 15:46:07.442',
///   'body': {
///             'suggestion_1.id': suggestion_1.toJson(),
///             'suggestion_2.id': suggestion_2.toJson(),
///             ...
///           }
/// }
///
/// Note that the only real difference between events is the body section,
/// which is mostly dependent on the toJson() of the objects of interest in the
/// event.
class Event {
  final EventType _type;

  /// The time associated with the creation (and thus, roughly, the occurrence)
  /// of this [Event].
  final DateTime _time;

  /// A description of this event in terms of the affected entities.
  dynamic _body;

  EventType get type => _type;
  DateTime get time => _time;
  dynamic get body => _body;

  Event._internal(this._type, this._body) : _time = new DateTime.now() {
    assert(body != null);
  }

  Event.fromSuggestionsAdded(final Iterable<Suggestion> added)
      : this._internal(EventType.SUGGESTIONS_ADDED, _iterableToJson(added));

  Event.fromSuggestionsRemoved(final Iterable<Suggestion> removed)
      : this._internal(EventType.SUGGESTIONS_REMOVED, _iterableToJson(removed));

  Event.fromSuggestionSelected(final Suggestion selected,
      {final Iterable<Suggestion> notSelected})
      : this._internal(EventType.SUGGESTION_SELECTED, {
          'selected': _iterableToJson([selected]),
          'notSelected': _iterableToJson(notSelected)
        });

  static List<Map<String, dynamic>> _iterableToJson(
          final Iterable<dynamic> values) =>
      values == null || values.isEmpty
          ? []
          : values.map((final dynamic value) => value.toJson()).toList();

  Map<String, dynamic> toInspectorJson() =>
      {'type': 'event-record', 'event': toJson()};

  Map<String, dynamic> toJson() => {'type': type, 'time': time, 'body': body};

  String toJsonString() =>
      JSON.encode(toJson(), toEncodable: (o) => o.toString());

  String toString() => toJsonString();
}
