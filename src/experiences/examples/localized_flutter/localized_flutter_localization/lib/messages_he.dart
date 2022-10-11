// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that provides messages for a he locale. All the
// messages from the main program should be duplicated here with the same
// function name.

import 'package:intl/intl.dart';
import 'package:intl/message_lookup_by_library.dart';

// ignore: unnecessary_new
final messages = MessageLookup();

// ignore: unused_element
final _keepAnalysisHappy = Intl.defaultLocale;

// ignore: non_constant_identifier_names
typedef MessageIfAbsent(String message_str, List args);

class MessageLookup extends MessageLookupByLibrary {
  get localeName => 'he';

  static m0(itemCount) => "${Intl.plural(itemCount, zero: 'אין הודעות.', one: 'יש הודעה אחת.', other: 'יש ${itemCount} הודעות.')}";

  final messages = _notInlinedMessages(_notInlinedMessages);
  static _notInlinedMessages(_) => <String, Function> {
    "appTitle" : MessageLookupByLibrary.simpleMessage("אפליקציה מתורגמת לשפה מקומית"),
    "bodyText" : m0,
    "footer" : MessageLookupByLibrary.simpleMessage("בקרוב: קריאת ההודעות האלה!")
  };
}
