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
import 'dart:convert';
import 'messages_all.dart' show evaluateJsonTemplate;

final messages = new MessageLookup();

typedef String MessageIfAbsent(String messageStr, List<dynamic> args);

class MessageLookup extends MessageLookupByLibrary {
  String get localeName => 'sr';

  String evaluateMessage(translation, List<dynamic> args) {
    return evaluateJsonTemplate(translation, args);
  }
  var _messages;
  get messages => _messages ??=
      const JsonDecoder().convert(messageText) as Map<String, dynamic>;
  static final messageText = r'''
{"ask":"УПИТ","auto":"Ауто","back":"Назад","batt":"Бат","battery":"Батерија","bluetooth":"Блутут","brightness":"Осветљеност","browser":"Прегледач","cancel":"Откажи","chrome":"Chrome","cpu":"ПРОЦ","date":"Датум","dateTime":"Датум и време","done":"Готово","fps":"Сл/с","ide":"ИДЕ","max":"Макс","mem":"МЕМ","memory":"Меморија","min":"Мин","mockWirelessNetwork":"Бежична_мрежа","music":"Музика","name":"Име","nameThisStory":"Именујте ову причу","network":"Мрежа","numThreads":["Intl.plural",0,[0," НИТИ"],[0," НИТ"],null,[0," НИТИ"],null,[0," НИТИ"]],"openPackage":["отвори ",0],"overview":"Преглед","pause":"Паузирај","pid":"ПИД","powerOff":"Искључи","recents":"Недавно","restart":"Рестартуј","runningTasks":["Intl.plural",0,[0," ЗАДАТАКА"],[0," ЗАДАТАК"],null,[0," ЗАДАТАКА"],null,[0," ЗАДАТАКА"]],"settings":"Подешавања","shutdown":"Искључи","signalStrong":"Јак сигнал","skip":"Прескочи","sleep":"Спавање","sunny":"Сунчано","tasks":"Задаци","topProcesses":"Процеси","totalTasks":["Intl.plural",0,0,0,null,0,null,0],"typeToAsk":"УНЕСИТЕ УПИТ","unit":"Јединица","volume":"Звук","weather":"Време","wireless":"Бежична"}''';
}