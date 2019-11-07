// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:intl/intl.dart';

import 'src/models/app_model.dart' show AppModel;
import 'src/widgets/app.dart' show App;

Future<void> main() async {
  setupLogger(name: 'ermine');

  final _intl = PropertyProviderProxy();
  StartupContext.fromStartupInfo().incoming.connectToService(_intl);

  final locales = LocaleSource(_intl);
  final model = AppModel();

  runApp(_Localized(model, locales.stream()));
  await model.onStarted();
  _intl.ctrl.close();
}

/// This is a localized version of the Ermine shell localized app.  It is the
/// same as the original App, but it also has the current locale injected.
class _Localized extends StatelessWidget {
  // The model to use for the underlying app.
  final AppModel _model;

  // The stream of locale updates.
  final Stream<Locale> _localeStream;

  const _Localized(this._model, this._localeStream);

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<Locale>(
        stream: _localeStream,
        builder: (BuildContext context, AsyncSnapshot<Locale> snapshot) {
          // There will always be a locale here.
          final Locale locale = snapshot.data;
          // This is required so app parts which don't depend on the flutter
          // locale have access to it.
          Intl.defaultLocale = locale.toString();
          return MaterialApp(
            home: App(model: _model),
            debugShowCheckedModeBanner: false,
            locale: locale,
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

