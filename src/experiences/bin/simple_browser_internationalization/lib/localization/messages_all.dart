// DO NOT EDIT. This is code generated via package:intl/generate_localized.dart
// This is a library that looks up messages for specific locales by
// delegating to the appropriate library.

// Ignore issues from commonly used lints in this file.
// ignore_for_file:implementation_imports, file_names, unnecessary_new
// ignore_for_file:unnecessary_brace_in_string_interps, directives_ordering
// ignore_for_file:argument_type_not_assignable, invalid_assignment
// ignore_for_file:prefer_single_quotes, prefer_generic_function_type_aliases
// ignore_for_file:comment_references

import 'dart:async';

import 'package:intl/intl.dart';
import 'package:intl/message_lookup_by_library.dart';
import 'package:intl/src/intl_helpers.dart';

import 'messages_ar-XB.dart' deferred as messages_ar_xb;
import 'messages_en-XA.dart' deferred as messages_en_xa;
import 'messages_en-XC.dart' deferred as messages_en_xc;
import 'messages_nl.dart' deferred as messages_nl;
import 'messages_sr-Latn.dart' deferred as messages_sr_latn;
import 'messages_sr.dart' deferred as messages_sr;

typedef Future<dynamic> LibraryLoader();
Map<String, LibraryLoader> _deferredLibraries = {
  'ar_XB': messages_ar_xb.loadLibrary,
  'en_XA': messages_en_xa.loadLibrary,
  'en_XC': messages_en_xc.loadLibrary,
  'nl': messages_nl.loadLibrary,
  'sr_Latn': messages_sr_latn.loadLibrary,
  'sr': messages_sr.loadLibrary,
};

MessageLookupByLibrary _findExact(String localeName) {
  switch (localeName) {
    case 'ar_XB':
      return messages_ar_xb.messages;
    case 'en_XA':
      return messages_en_xa.messages;
    case 'en_XC':
      return messages_en_xc.messages;
    case 'nl':
      return messages_nl.messages;
    case 'sr_Latn':
      return messages_sr_latn.messages;
    case 'sr':
      return messages_sr.messages;
    default:
      return null;
  }
}

/// User programs should call this before using [localeName] for messages.
Future<bool> initializeMessages(String localeName) async {
  var availableLocale = Intl.verifiedLocale(
    localeName,
    (locale) => _deferredLibraries[locale] != null,
    onFailure: (_) => null);
  if (availableLocale == null) {
    return new Future.value(false);
  }
  var lib = _deferredLibraries[availableLocale];
  await (lib == null ? new Future.value(false) : lib());
  initializeInternalMessageLookup(() => new CompositeMessageLookup());
  messageLookup.addLocale(availableLocale, _findGeneratedMessagesFor);
  return new Future.value(true);
}

bool _messagesExistFor(String locale) {
  try {
    return _findExact(locale) != null;
  } catch (e) {
    return false;
  }
}

MessageLookupByLibrary _findGeneratedMessagesFor(String locale) {
  var actualLocale = Intl.verifiedLocale(locale, _messagesExistFor,
      onFailure: (_) => null);
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
String evaluateJsonTemplate(dynamic input, List<dynamic> args) {
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
         Intl.pluralLogic(
             howMany,
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
         Intl.genderLogic(
             gender,
             female: template[2],
             male: template[3],
             other: template[4]),
         args);
   }
   if (messageName == "Intl.select") {
     var select = args[template[1]];
     var choices = template[2];
     return evaluateJsonTemplate(Intl.selectLogic(select, choices), args);
   }

   // If we get this far, then we are a basic interpolation, just strings and
   // ints.
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

 