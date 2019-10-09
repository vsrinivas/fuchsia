// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that looks up messages for specific locales by
// delegating to the appropriate library.

import 'dart:async';

import 'package:intl/intl.dart';
import 'package:intl/message_lookup_by_library.dart';
// ignore: implementation_imports
import 'package:intl/src/intl_helpers.dart';

import 'messages_ar-XB.dart' deferred as messages_ar_xb;
import 'messages_en-XA.dart' deferred as messages_en_xa;
import 'messages_en-XC.dart' deferred as messages_en_xc;
import 'messages_he.dart' deferred as messages_he;
import 'messages_sr-Latn.dart' deferred as messages_sr_latn;
import 'messages_sr.dart' deferred as messages_sr;

typedef Future<dynamic> LibraryLoader();
Map<String, LibraryLoader> _deferredLibraries = {
  'ar_XB': messages_ar_xb.loadLibrary,
  'en_XA': messages_en_xa.loadLibrary,
  'en_XC': messages_en_xc.loadLibrary,
  'sr_Latn': messages_sr_latn.loadLibrary,
  'he': messages_he.loadLibrary,
  'sr': messages_sr.loadLibrary,
};

MessageLookupByLibrary _findExact(localeName) {
  switch (localeName) {
    case 'ar_XB':
      return messages_ar_xb.messages;
    case 'en_XA':
      return messages_en_xa.messages;
    case 'en_XC':
      return messages_en_xc.messages;
    case 'sr_Latn':
      return messages_sr_latn.messages;
    case 'sr':
      return messages_sr.messages;
    case 'he':
      return messages_he.messages;
    default:
      return null;
  }
}

/// User programs should call this before using [localeName] for messages.
Future<bool> initializeMessages(String localeName) async {
  var availableLocale = Intl.verifiedLocale(
      localeName, (locale) => _deferredLibraries[locale] != null,
      onFailure: (_) => null);
  if (availableLocale == null) {
    // ignore: unnecessary_new
    return new Future.value(false);
  }
  var lib = _deferredLibraries[availableLocale];
  // ignore: unnecessary_new
  await (lib == null ? new Future.value(false) : lib());
  // ignore: unnecessary_new
  initializeInternalMessageLookup(() => new CompositeMessageLookup());
  messageLookup.addLocale(availableLocale, _findGeneratedMessagesFor);
  // ignore: unnecessary_new
  return new Future.value(true);
}

bool _messagesExistFor(String locale) {
  try {
    return _findExact(locale) != null;
  } catch (e) {
    return false;
  }
}

MessageLookupByLibrary _findGeneratedMessagesFor(locale) {
  var actualLocale =
      Intl.verifiedLocale(locale, _messagesExistFor, onFailure: (_) => null);
  if (actualLocale == null) return null;
  return _findExact(actualLocale);
}

/// Turn the JSON template into a string.
///
/// We expect one of the following forms for the template.
/// * null -> null
/// * String s -> s
/// * int n -> '${args[n]}'
/// * List list, one of
///   * ['Intl.plural', int howMany, (templates for zero, one, ...)]
///   * ['Intl.gender', String gender, (templates for female, male, other)]
///   * ['Intl.select', String choice, { 'case' : template, ...} ]
///   * ['text alternating with ', 0 , ' indexes in the argument list']
String evaluateJsonTemplate(Object input, List<dynamic> args) {
  if (input == null) return null;
  if (input is String) return input;
  if (input is int) {
    return "${args[input]}";
  }

  List<dynamic> template = input;
  var messageName = template.first;
  if (messageName == "Intl.plural") {
    var howMany = args[template[1]];
    return evaluateJsonTemplate(
        Intl.pluralLogic(howMany,
            zero: template[2],
            one: template[3],
            two: template[4],
            few: template[5],
            many: template[6],
            other: template[7]),
        args);
  }
  if (messageName == "Intl.gender") {
    var gender = args[template[1]];
    return evaluateJsonTemplate(
        Intl.genderLogic(gender,
            female: template[2], male: template[3], other: template[4]),
        args);
  }
  if (messageName == "Intl.select") {
    var select = args[template[1]];
    var choices = template[2];
    return evaluateJsonTemplate(Intl.selectLogic(select, choices), args);
  }

  // If we get this far, then we are a basic interpolation, just strings and
  // ints.
  // ignore: unnecessary_new
  var output = new StringBuffer();
  for (var entry in template) {
    if (entry is int) {
      output.write("${args[entry]}");
    } else {
      output.write("$entry");
    }
  }
  return output.toString();
}
