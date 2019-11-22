// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that provides messages for a sr_Latn locale. All the
// messages from the main program should be duplicated here with the same
// function name.

// Ignore issues from commonly used lints in this file.
// ignore_for_file:unnecessary_brace_in_string_interps, unnecessary_new
// ignore_for_file:prefer_single_quotes,comment_references, directives_ordering
// ignore_for_file:annotate_overrides,prefer_generic_function_type_aliases
// ignore_for_file:unused_import, file_names

import 'package:intl/intl.dart';
import 'package:intl/message_lookup_by_library.dart';
import 'dart:convert';
import 'messages_all.dart' show evaluateJsonTemplate;

final messages = new MessageLookup();

typedef String MessageIfAbsent(String messageStr, List<dynamic> args);

class MessageLookup extends MessageLookupByLibrary {
  String get localeName => 'sr_Latn';

  String evaluateMessage(translation, List<dynamic> args) {
    return evaluateJsonTemplate(translation, args);
  }
  var _messages;
  get messages => _messages ??=
      const JsonDecoder().convert(messageText) as Map<String, dynamic>;
  static final messageText = r'''
{"ask":"UPIT","auto":"Auto","back":"Nazad","batt":"Bat","battery":"Baterija","bluetooth":"Blutut","brightness":"Osvetljenost","browser":"Pregledač","cancel":"Otkaži","chrome":"Chrome","cpu":"PROC","date":"Datum","dateTime":"Datum i vreme","done":"Gotovo","fps":"Sl/s","ide":"IDE","max":"Maks","mem":"MEM","memory":"Memorija","min":"Min","mockWirelessNetwork":"Bežična_mreža","music":"Muzika","name":"Ime","nameThisStory":"Imenujte ovu priču","network":"Mreža","numThreads":["Intl.plural",0,[0," NITI"],[0," NIT"],null,[0," NITI"],null,[0," NITI"]],"openPackage":["otvori ",0],"overview":"Pregled","pause":"Pauziraj","pid":"PID","powerOff":"Isključi","recents":"Nedavno","restart":"Restartuj","runningTasks":["Intl.plural",0,[0," ZADATAKA"],[0," ZADATAK"],null,[0," ZADATAKA"],null,[0," ZADATAKA"]],"settings":"Podešavanja","shutdown":"Isključi","signalStrong":"Jak signal","skip":"Preskoči","sleep":"Spavanje","sunny":"Sunčano","tasks":"Zadaci","topProcesses":"Procesi","totalTasks":["Intl.plural",0,0,0,null,0,null,0],"typeToAsk":"UNESITE UPIT","unit":"Jedinica","volume":"Zvuk","weather":"Vreme","wireless":"Bežična"}''';
}