// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:lib.widgets/model.dart';

import 'package:fuchsia_logger/logger.dart';
import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:intl/intl.dart';

import 'src/models/app_model.dart' show AppModel;
import 'src/widgets/app.dart' show App;

Future<void> main() async {
  // TODO(fmil): Add a dynamically changing value of the locale, based on system
  // preferences.
  final providers = Providers()
    ..provideValue(CurrentLocale(Locale.fromSubtags(languageCode: 'en')));

  setupLogger(name: 'ermine');

  final model = AppModel();
  final app = _Localized(model);

  runApp(ProviderNode(providers: providers, child: app));

  await model.onStarted();
}

/// This is a localized version of the Ermine shell localized app.  It is the
/// same as the original App, but it also has the current locale injected.
class _Localized extends StatelessWidget {
  final AppModel _model;
  const _Localized(this._model);

  @override
  Widget build(BuildContext context) {
    return Provide<CurrentLocale>(
        builder: (BuildContext context, Widget child, currentLocale) {
      // Changing the default locale here ensures that any non-Flutter code
      // sees the locale change as well.
      Intl.defaultLocale = currentLocale.toString();
      return MaterialApp(
        home: App(model: _model),
        debugShowCheckedModeBanner: false,
        locale: currentLocale.value,
        localizationsDelegates: [
          localizations.delegate(),
          GlobalMaterialLocalizations.delegate,
          GlobalWidgetsLocalizations.delegate,
        ],
        supportedLocales: supported_locales.locales,
      );
    });
  }
}

/// Holds the current [Locale] and notifies any listeners of value changes.
class CurrentLocale extends ValueNotifier<Locale> {
  CurrentLocale(Locale value) : super(value);
}
