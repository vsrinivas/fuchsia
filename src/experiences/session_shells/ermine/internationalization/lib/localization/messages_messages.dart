// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that provides messages for a messages locale. All the
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
  String get localeName => 'messages';

  static m0(numThreads) => "${Intl.plural(numThreads, zero: '${numThreads} THR', one: '${numThreads} THR', other: '${numThreads} THR')}";

  static m1(numTasks) => "${Intl.plural(numTasks, zero: '${numTasks} RUNNING', one: '${numTasks} RUNNING', other: '${numTasks} RUNNING')}";

  static m2(numTasks) => "${Intl.plural(numTasks, zero: '${numTasks}', one: '${numTasks}', other: '${numTasks}')}";

  final messages = _notInlinedMessages(_notInlinedMessages);
  static _notInlinedMessages(_) => <String, Function> {
    "ask" : MessageLookupByLibrary.simpleMessage("ASK"),
    "auto" : MessageLookupByLibrary.simpleMessage("Auto"),
    "back" : MessageLookupByLibrary.simpleMessage("Back"),
    "batt" : MessageLookupByLibrary.simpleMessage("Batt"),
    "battery" : MessageLookupByLibrary.simpleMessage("Battery"),
    "brightness" : MessageLookupByLibrary.simpleMessage("Brightness"),
    "browser" : MessageLookupByLibrary.simpleMessage("Browser"),
    "cancel" : MessageLookupByLibrary.simpleMessage("Cancel"),
    "chrome" : MessageLookupByLibrary.simpleMessage("Chrome"),
    "cpu" : MessageLookupByLibrary.simpleMessage("CPU"),
    "date" : MessageLookupByLibrary.simpleMessage("Date"),
    "done" : MessageLookupByLibrary.simpleMessage("Done"),
    "fps" : MessageLookupByLibrary.simpleMessage("FPS"),
    "ide" : MessageLookupByLibrary.simpleMessage("IDE"),
    "max" : MessageLookupByLibrary.simpleMessage("Max"),
    "mem" : MessageLookupByLibrary.simpleMessage("MEM"),
    "memory" : MessageLookupByLibrary.simpleMessage("Memory"),
    "min" : MessageLookupByLibrary.simpleMessage("Min"),
    "mockWirelessNetwork" : MessageLookupByLibrary.simpleMessage("Wireless_Network"),
    "music" : MessageLookupByLibrary.simpleMessage("Music"),
    "name" : MessageLookupByLibrary.simpleMessage("Name"),
    "nameThisStory" : MessageLookupByLibrary.simpleMessage("Name this story"),
    "network" : MessageLookupByLibrary.simpleMessage("Network"),
    "numThreads" : m0,
    "overview" : MessageLookupByLibrary.simpleMessage("Overview"),
    "pause" : MessageLookupByLibrary.simpleMessage("Pause"),
    "pid" : MessageLookupByLibrary.simpleMessage("PID"),
    "powerOff" : MessageLookupByLibrary.simpleMessage("Power Off"),
    "recents" : MessageLookupByLibrary.simpleMessage("Recents"),
    "restart" : MessageLookupByLibrary.simpleMessage("Restart"),
    "runningTasks" : m1,
    "settings" : MessageLookupByLibrary.simpleMessage("Settings"),
    "shutdown" : MessageLookupByLibrary.simpleMessage("Shutdown"),
    "signalStrong" : MessageLookupByLibrary.simpleMessage("Strong Signal"),
    "skip" : MessageLookupByLibrary.simpleMessage("Skip"),
    "sleep" : MessageLookupByLibrary.simpleMessage("Sleep"),
    "sunny" : MessageLookupByLibrary.simpleMessage("Sunny"),
    "tasks" : MessageLookupByLibrary.simpleMessage("TASKS"),
    "topProcesses" : MessageLookupByLibrary.simpleMessage("Top Processes"),
    "totalTasks" : m2,
    "typeToAsk" : MessageLookupByLibrary.simpleMessage("TYPE TO ASK"),
    "volume" : MessageLookupByLibrary.simpleMessage("Volume"),
    "weather" : MessageLookupByLibrary.simpleMessage("Weather"),
    "wireless" : MessageLookupByLibrary.simpleMessage("Wireless")
  };
}
