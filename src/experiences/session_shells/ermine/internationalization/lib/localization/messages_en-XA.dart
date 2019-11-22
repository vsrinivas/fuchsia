// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that provides messages for a en_XA locale. All the
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
  String get localeName => 'en_XA';

  String evaluateMessage(translation, List<dynamic> args) {
    return evaluateJsonTemplate(translation, args);
  }
  var _messages;
  get messages => _messages ??=
      const JsonDecoder().convert(messageText) as Map<String, dynamic>;
  static final messageText = r'''
{"ask":"[ÅŠĶ one]","auto":"[Åûţö one]","back":"[Бåçķ one]","batt":"[Бåţţ one]","battery":"[Бåţţéŕý one]","bluetooth":"[Бļûéţööţĥ one two]","brightness":"[Бŕîĝĥţñéšš one two]","browser":"[Бŕöŵšéŕ one]","cancel":"[Çåñçéļ one]","chrome":"[Çĥŕömé one]","cpu":"[ÇÞÛ one]","date":"[Ðåţé one]","dateTime":"[Ðåţé & Ţîmé one two]","disconnect":"[ÐÎŠÇÖÑÑÉÇŢ one two]","done":"[Ðöñé one]","fps":"[FÞŠ one]","ide":"[ÎÐÉ one]","logout":"[Ļöĝöûţ one]","max":"[Måx one]","mem":"[MÉM one]","memory":"[Mémöŕý one]","min":"[Mîñ one]","mockWirelessNetwork":"[Ŵîŕéļéšš_Ñéţŵöŕķ one two]","music":"[Mûšîç one]","name":"[Ñåmé one]","nameThisStory":"[Ñåmé ţĥîš šţöŕý one two]","network":"[Ñéţŵöŕķ one]","numThreads":["Intl.plural",0,["[ᐅ",0,"ᐊ ŢĤŔ one two]"],["[ᐅ",0,"ᐊ ŢĤŔ one two]"],null,null,null,["[ᐅ",0,"ᐊ ŢĤŔ one two]"]],"openPackage":["[öþéñ ᐅ",0,"ᐊ one two]"],"overview":"[Övéŕvîéŵ one]","pause":"[Þåûšé one]","pid":"[ÞÎÐ one]","powerOff":"[Þöŵéŕ Öƒƒ one two]","recents":"[Ŕéçéñţš one]","restart":"[Ŕéšţåŕţ one]","runningTasks":["Intl.plural",0,["[ᐅ",0,"ᐊ ŔÛÑÑÎÑĜ one two three]"],["[ᐅ",0,"ᐊ ŔÛÑÑÎÑĜ one two three]"],null,null,null,["[ᐅ",0,"ᐊ ŔÛÑÑÎÑĜ one two three]"]],"settings":"[Šéţţîñĝš one]","shutdown":"[Šĥûţðöŵñ one]","signalStrong":"[Šţŕöñĝ Šîĝñåļ one two]","skip":"[Šķîþ one]","sleep":"[Šļééþ one]","sunny":"[Šûññý one]","tasks":"[ŢÅŠĶŠ one]","timezone":"[Ţîméžöñé one]","topProcesses":"[Ţöþ Þŕöçéššéš one two]","totalTasks":["Intl.plural",0,["[ᐅ",0,"ᐊ one]"],["[ᐅ",0,"ᐊ one]"],null,null,null,["[ᐅ",0,"ᐊ one]"]],"typeToAsk":"[ŢÝÞÉ ŢÖ ÅŠĶ one two]","unit":"[Ûñîţ one]","volume":"[Vöļûmé one]","weather":"[Ŵéåţĥéŕ one]","wireless":"[Ŵîŕéļéšš one]"}''';
}