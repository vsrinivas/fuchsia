// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Should the localizations delegate be generated instead?

import 'dart:async';
import 'dart:ui' show Locale;

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:intl/intl.dart';

import 'localization/messages_all.dart' as messages_all;
import 'supported_locales.dart' as supported_locales;

LocalizationsDelegate<void> delegate() => _LocalizationsDelegate();

class _LocalizationsDelegate extends LocalizationsDelegate<void> {
  static Future<void> loadLocale(Locale locale) async {
    final String name =
        (locale.countryCode == null || locale.countryCode?.isEmpty == true)
            ? locale.languageCode
            : locale.toString();
    final String localeName = Intl.canonicalizedLocale(name);
    await messages_all.initializeMessages(localeName);
    log.info('Setting default locale to: ${Intl.defaultLocale}');
  }

  const _LocalizationsDelegate();

  @override
  Future<void> load(Locale locale) => loadLocale(locale);

  // For the time being, never reload.
  @override
  bool shouldReload(_LocalizationsDelegate __) => false;

  @override
  bool isSupported(Locale locale) {
    return supported_locales.locales.contains(locale);
  }
}
