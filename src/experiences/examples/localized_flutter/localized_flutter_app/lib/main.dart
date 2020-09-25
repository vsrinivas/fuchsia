// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter/material.dart';

import 'localized_mod_localizations_delegate.dart' as localizations_delegate;
import 'localized_mod_strings.dart';
import 'provider.dart';
import 'supported_locales.dart' as supported_locales;

void main() {
  final providers = Providers()..provideValue(CurrentLocale(null));

  runApp(ProviderNode(providers: providers, child: LocalizedMod()));
  // TODO(fxbug.dev/8745): Receive i18n settings from the View interface.
}

/// The main application widget for Localized Mod.
class LocalizedMod extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Provide<CurrentLocale>(
        builder: (BuildContext context, Widget child, currentLocale) =>
            MaterialApp(
              onGenerateTitle: (BuildContext context) =>
                  LocalizedModStrings.appTitle,
              localizationsDelegates:
                  localizations_delegate.allLocalizationsDelegates,
              supportedLocales: supported_locales.supportedLocales,
              locale: currentLocale.value,
              home: _LocalizedModHome(Random().nextInt(100)),
            ));
  }
}

/// The root layout widget for Localized Mod.
class _LocalizedModHome extends StatelessWidget {
  static final List<DropdownMenuItem<Locale>> dropdownItems = supported_locales
      .supportedLocales
      .map((Locale locale) =>
          DropdownMenuItem(value: locale, child: Text(locale.toString())))
      .toList();

  final int messageCount;

  const _LocalizedModHome(this.messageCount) : super();

  @override
  Widget build(BuildContext context) {
    return Scaffold(
        appBar: AppBar(
          title: Text(LocalizedModStrings.appTitle),
        ),
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              DropdownButton<Locale>(
                  value: Localizations.localeOf(context),
                  items: dropdownItems,
                  onChanged: (newValue) =>
                      Provide.value<CurrentLocale>(context).value = newValue),
              Container(
                  margin: EdgeInsets.symmetric(vertical: 40),
                  child: Text(LocalizedModStrings.bodyText(messageCount))),
              Container(
                  margin: EdgeInsets.symmetric(vertical: 40),
                  child: Text(LocalizedModStrings.footer)),
            ],
          ),
        ));
  }
}

/// Holds the current [Locale] and notifies any listeners when it changes.
class CurrentLocale extends ValueNotifier<Locale> {
  CurrentLocale(Locale value) : super(value);
}
