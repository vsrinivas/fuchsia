// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart';
import 'package:localized_flutter_localization/messages_all.dart'
    as messages_all;

import 'supported_locales.dart';

/// A [LocalizationsDelegate] for Localized Mod that connects Flutter's
/// localization updates to the `intl` library's translation loading.
///
/// Every mod that uses localized strings needs to have a
/// `LocalizationsDelegate`.
class LocalizedModLocalizationsDelegate extends LocalizationsDelegate<void> {
  /// Loads the translations for the given locale.
  static Future<void> loadLocale(Locale locale) async {
    final String name =
        (locale.countryCode == null || locale.countryCode.isEmpty)
            ? locale.languageCode
            : locale.toString();
    final String localeName = Intl.canonicalizedLocale(name);
    await messages_all.initializeMessages(localeName);
    Intl.defaultLocale = localeName;
  }

  const LocalizedModLocalizationsDelegate();

  @override
  Future<void> load(Locale locale) => loadLocale(locale);

  @override
  bool shouldReload(LocalizedModLocalizationsDelegate _) => false;

  @override
  bool isSupported(Locale locale) => supportedLocales.contains(locale);
}

const List<LocalizationsDelegate> allLocalizationsDelegates = [
  // Delegate containing all app-level messages
  LocalizedModLocalizationsDelegate(),
  // Flutter-provided delegates for Flutter UI messages
  ...GlobalMaterialLocalizations.delegates,
  GlobalWidgetsLocalizations.delegate,
];
