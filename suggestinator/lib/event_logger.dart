// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:logging/logging.dart';

import 'suggestion.dart';

enum EventType { SUGGESTIONS_REMOVED, SUGGESTIONS_ADDED, SUGGESTION_SELECTED, }

class EventLogger {
  static final Logger _log = new Logger.detached("EventLogger");

  /// Returns a stream of logged events.
  Stream<EventRecord> get onRecord => _log.onRecord
      .map((final LogRecord r) => new EventRecord.fromLogRecord(r));

  /// Publishes [event] to the log.
  void log(final Event event) {
    // TODO(dennischeng): Unhack this use of [hierarchicalLoggingEnabled],
    // which is used to avoid publishing event logs on the root logger.
    hierarchicalLoggingEnabled = true;
    _log.log(Level.INFO, event);
    hierarchicalLoggingEnabled = false;
  }
}

/// The representation of an [Event] object in the log. Similar to a [LogRecord]
/// object, but only contains a single piece of metadata, creation time.
///
/// [EventRecord]s will have the following JSON form (the example below is for
/// a SUGGESTIONS_REMOVED event):
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
class EventRecord {
  final Event _event;

  /// The time associated with the insertion of this record into the log.
  final DateTime _time;

  EventRecord._internal(this._event, this._time);

  /// Extracts an [Event] object from a [LogRecord], along with the record's
  /// creation timestamp.
  factory EventRecord.fromLogRecord(final LogRecord r) {
    if (r.object is! Event) {
      throw new Exception(
          'An EventRecord can only be initialized from an event LogRecord.');
    }
    return new EventRecord._internal(r.object, r.time);
  }

  Map<String, dynamic> toJson() =>
      {'type': _event.type, 'time': _time, 'body': _event.body};

  String toString() =>
      'type: ${_event.type}\ntime: $_time\nbody: ${_event.body}';
}

/// Encodes important changes and moments in Modular.
class Event {
  /// A description of this event in terms of the affected entities.
  final Map<String, dynamic> _body;
  final EventType _type;

  Map<String, dynamic> get body => _body;
  EventType get type => _type;

  Event._internal(this._body, this._type) {
    assert(body != null);
  }

  Event.fromSuggestionsAdded(final Iterable<Suggestion> added)
      : this._internal(_suggestionsToJson(added), EventType.SUGGESTIONS_ADDED);

  Event.fromSuggestionsRemoved(final Iterable<Suggestion> removed)
      : this._internal(
            _suggestionsToJson(removed), EventType.SUGGESTIONS_REMOVED);

  Event.fromSuggestionSelected(final Suggestion selected,
      {final Iterable<Suggestion> notSelected})
      : this._internal({
          'selected': _suggestionsToJson([selected]),
          'notSelected':
              notSelected == null ? [] : _suggestionsToJson(notSelected)
        }, EventType.SUGGESTION_SELECTED);

  // Converts a list of Suggestion objects into a map from each Suggestion's id
  // to its JSON representation.
  static Map<String, dynamic> _suggestionsToJson(
      final Iterable<Suggestion> suggestions) {
    return new Map.fromIterable(suggestions,
        key: (final Suggestion s) => s.id.toString(),
        value: (final Suggestion s) => s.toJson());
  }
}
