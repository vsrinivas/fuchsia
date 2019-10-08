// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that provides messages for a sr locale. All the
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
  String get localeName => 'sr';

  static m0(numThreads) => "${Intl.plural(numThreads, zero: '${numThreads} НИТИ', one: '${numThreads} НИТ', other: '${numThreads} НИТИ')}";

  static m1(numTasks) => "${Intl.plural(numTasks, zero: '${numTasks} ЗАДАТАКА', one: '${numTasks} ЗАДАТАК', other: '${numTasks} ЗАДАТАКА')}";

  static m2(numTasks) => "${Intl.plural(numTasks, zero: '${numTasks}', one: '${numTasks}', other: '${numTasks}')}";

  final messages = _notInlinedMessages(_notInlinedMessages);
  static _notInlinedMessages(_) => <String, Function> {
    "ask" : MessageLookupByLibrary.simpleMessage("УПИТ"),
    "auto" : MessageLookupByLibrary.simpleMessage("Ауто"),
    "back" : MessageLookupByLibrary.simpleMessage("НАЗАД"),
    "batt" : MessageLookupByLibrary.simpleMessage("БАТ"),
    "battery" : MessageLookupByLibrary.simpleMessage("Батерија"),
    "brightness" : MessageLookupByLibrary.simpleMessage("ОСВЕТЉАЈ"),
    "browser" : MessageLookupByLibrary.simpleMessage("Прегледач"),
    "cancel" : MessageLookupByLibrary.simpleMessage("Откажи"),
    "cpu" : MessageLookupByLibrary.simpleMessage("ПРОЦ"),
    "date" : MessageLookupByLibrary.simpleMessage("ДАТУМ"),
    "done" : MessageLookupByLibrary.simpleMessage("Готово"),
    "fps" : MessageLookupByLibrary.simpleMessage("СЛ/СЕК"),
    "max" : MessageLookupByLibrary.simpleMessage("МАКС"),
    "memory" : MessageLookupByLibrary.simpleMessage("МЕМОРИЈА"),
    "min" : MessageLookupByLibrary.simpleMessage("МИН"),
    "mockWirelessNetwork" : MessageLookupByLibrary.simpleMessage("Бежична_мрежа"),
    "music" : MessageLookupByLibrary.simpleMessage("МУЗИКА"),
    "name" : MessageLookupByLibrary.simpleMessage("ИМЕ"),
    "nameThisStory" : MessageLookupByLibrary.simpleMessage("Именуј ову причу"),
    "network" : MessageLookupByLibrary.simpleMessage("МРЕЖА"),
    "numThreads" : m0,
    "overview" : MessageLookupByLibrary.simpleMessage("Преглед"),
    "pause" : MessageLookupByLibrary.simpleMessage("ПАУЗА"),
    "powerOff" : MessageLookupByLibrary.simpleMessage("ИСКЉУЧИ"),
    "recents" : MessageLookupByLibrary.simpleMessage("Историјат"),
    "restart" : MessageLookupByLibrary.simpleMessage("РЕСТАРТ"),
    "runningTasks" : m1,
    "settings" : MessageLookupByLibrary.simpleMessage("ПОДЕШАВАЊА"),
    "shutdown" : MessageLookupByLibrary.simpleMessage("Искључи"),
    "signalStrong" : MessageLookupByLibrary.simpleMessage("ЈАК СИГНАЛ"),
    "skip" : MessageLookupByLibrary.simpleMessage("ПРЕСКОЧИ"),
    "sleep" : MessageLookupByLibrary.simpleMessage("СПАВАЊЕ"),
    "sunny" : MessageLookupByLibrary.simpleMessage("СУНЧАНО"),
    "tasks" : MessageLookupByLibrary.simpleMessage("ЗАДАЦИ"),
    "topProcesses" : MessageLookupByLibrary.simpleMessage("ВРШНИ ПРОЦЕСИ"),
    "totalTasks" : m2,
    "typeToAsk" : MessageLookupByLibrary.simpleMessage("УНЕСИТЕ УПИТ"),
    "volume" : MessageLookupByLibrary.simpleMessage("ЈАЧИНА"),
    "weather" : MessageLookupByLibrary.simpleMessage("ВРЕМЕ"),
    "wireless" : MessageLookupByLibrary.simpleMessage("БЕЖИЧНА")
  };
}
