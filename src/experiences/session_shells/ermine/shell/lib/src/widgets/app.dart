// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:flutter_localizations/flutter_localizations.dart';

import '../models/app_model.dart';
import '../utils/styles.dart';

import 'support/home_container.dart';
import 'support/overview.dart';
import 'support/recents.dart';

/// Builds the main display of this session shell.
class App extends StatelessWidget {
  final AppModel model;

  const App({@required this.model});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<Locale>(
        stream: model.localeStream,
        builder: (context, snapshot) {
          final locale = snapshot.data;
          return MaterialApp(
            debugShowCheckedModeBanner: false,
            theme: ErmineStyle.kErmineTheme,
            locale: locale,
            localizationsDelegates: [
              localizations.delegate(),
              GlobalMaterialLocalizations.delegate,
              GlobalWidgetsLocalizations.delegate,
            ],
            supportedLocales: supported_locales.locales,
            home: Material(
                color: ErmineStyle.kBackgroundColor,
                child: Stack(
                  fit: StackFit.expand,
                  children: <Widget>[
                    // Recents.
                    RecentsContainer(model: model),

                    // Overview or Home.
                    AnimatedBuilder(
                      animation: model.overviewVisibility,
                      builder: (context, _) => model.overviewVisibility.value
                          ? OverviewContainer(model: model)
                          : HomeContainer(model: model),
                    ),
                  ],
                )),
          );
        });
  }
}
