// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that provides messages for a nl locale. All the
// messages from the main program should be duplicated here with the same
// function name.

// Ignore issues from commonly used lints in this file.
// ignore_for_file:unnecessary_brace_in_string_interps, unnecessary_new
// ignore_for_file:prefer_single_quotes,comment_references, directives_ordering
// ignore_for_file:annotate_overrides,prefer_generic_function_type_aliases
// ignore_for_file:unused_import, file_names

import 'package:intl/intl.dart';
import 'package:intl/message_lookup_by_library.dart';

final messages = new MessageLookup();

typedef String MessageIfAbsent(String messageStr, List<dynamic> args);

class MessageLookup extends MessageLookupByLibrary {
  String get localeName => 'nl';

  static m0(numThreads) => "${Intl.plural(numThreads, zero: '${numThreads} DRAA', one: '${numThreads} DRAA', other: '${numThreads} DRAA')}";

  static m1(numTasks) => "${Intl.plural(numTasks, zero: '${numTasks} LOPENDE', one: '${numTasks} LOPEND', other: '${numTasks} LOPENDE')}";

  static m2(numTasks) => "${Intl.plural(numTasks, zero: '${numTasks}', one: '${numTasks}', other: '${numTasks}')}";

  final messages = _notInlinedMessages(_notInlinedMessages);
  static _notInlinedMessages(_) => <String, Function> {
    "ask" : MessageLookupByLibrary.simpleMessage("ZOEKT"),
    "auto" : MessageLookupByLibrary.simpleMessage("Auto"),
    "back" : MessageLookupByLibrary.simpleMessage("TERUG"),
    "batt" : MessageLookupByLibrary.simpleMessage("BATT"),
    "battery" : MessageLookupByLibrary.simpleMessage("Batterij"),
    "brightness" : MessageLookupByLibrary.simpleMessage("HELDERHEID"),
    "browser" : MessageLookupByLibrary.simpleMessage("Browser"),
    "cancel" : MessageLookupByLibrary.simpleMessage("Annuleren"),
    "cpu" : MessageLookupByLibrary.simpleMessage("CPU"),
    "date" : MessageLookupByLibrary.simpleMessage("DATUM"),
    "done" : MessageLookupByLibrary.simpleMessage("Klaar"),
    "fps" : MessageLookupByLibrary.simpleMessage("FPS"),
    "max" : MessageLookupByLibrary.simpleMessage("MAX"),
    "memory" : MessageLookupByLibrary.simpleMessage("GEHEUGEN"),
    "min" : MessageLookupByLibrary.simpleMessage("MIN"),
    "mockWirelessNetwork" : MessageLookupByLibrary.simpleMessage("Draadlooz_Netwerk"),
    "music" : MessageLookupByLibrary.simpleMessage("MUZIEK"),
    "name" : MessageLookupByLibrary.simpleMessage("Naam"),
    "nameThisStory" : MessageLookupByLibrary.simpleMessage("Noem dit verhaal"),
    "network" : MessageLookupByLibrary.simpleMessage("NETWERK"),
    "numThreads" : m0,
    "overview" : MessageLookupByLibrary.simpleMessage("Overzicht"),
    "pause" : MessageLookupByLibrary.simpleMessage("PAUZE"),
    "powerOff" : MessageLookupByLibrary.simpleMessage("UITSCHAKELEN"),
    "recents" : MessageLookupByLibrary.simpleMessage("Onlangs"),
    "restart" : MessageLookupByLibrary.simpleMessage("HERSTARTEN"),
    "runningTasks" : m1,
    "settings" : MessageLookupByLibrary.simpleMessage("INSTELLINGEN"),
    "shutdown" : MessageLookupByLibrary.simpleMessage("Uitschakelen"),
    "signalStrong" : MessageLookupByLibrary.simpleMessage("STERK SIGNAAL"),
    "skip" : MessageLookupByLibrary.simpleMessage("OVERSLAAN"),
    "sleep" : MessageLookupByLibrary.simpleMessage("SLAAP"),
    "sunny" : MessageLookupByLibrary.simpleMessage("ZONNIG"),
    "tasks" : MessageLookupByLibrary.simpleMessage("TAKEN"),
    "topProcesses" : MessageLookupByLibrary.simpleMessage("TOP PROCESSEN"),
    "totalTasks" : m2,
    "typeToAsk" : MessageLookupByLibrary.simpleMessage("ZOEKT EN GIJ ZAL SPINAZIE ETEN"),
    "volume" : MessageLookupByLibrary.simpleMessage("GELUID"),
    "weather" : MessageLookupByLibrary.simpleMessage("WEER"),
    "wireless" : MessageLookupByLibrary.simpleMessage("DRAADLOZE")
  };
}
