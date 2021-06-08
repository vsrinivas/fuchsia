// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:intl/intl.dart';

import '../models/app_model.dart';
import '../models/oobe_model.dart';
import '../utils/styles.dart';
import 'support/alert.dart';
import 'support/home_container.dart';
import 'support/oobe.dart';
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
          // Check if [Locale] is loaded.
          if (!snapshot.hasData) {
            return Offstage();
          }
          final locale = snapshot.data;
          // Needed to set the locale for anything that depends on the Intl
          // package.
          Intl.defaultLocale = locale.toString();
          return MaterialApp(
            debugShowCheckedModeBanner: false,
            theme: ErmineStyle.kErmineTheme,
            locale: locale,
            localizationsDelegates: [
              localizations.delegate(),
              GlobalMaterialLocalizations.delegate,
              GlobalWidgetsLocalizations.delegate,
            ],
            scrollBehavior: MaterialScrollBehavior().copyWith(
              dragDevices: {PointerDeviceKind.mouse, PointerDeviceKind.touch},
            ),
            supportedLocales: supported_locales.locales,
            home: Material(
                color: ErmineStyle.kBackgroundColor,
                child: Stack(
                  fit: StackFit.expand,
                  children: <Widget>[
                    // Recents.
                    buildRecents(model),

                    // OOBE or Overview or Home.
                    AnimatedBuilder(
                      animation: Listenable.merge([
                        model.overviewVisibility,
                        model.oobeVisibility,
                      ]),
                      builder: (context, _) => model.oobeVisibility.value
                          ? buildOobe(model)
                          : model.overviewVisibility.value
                              ? buildOverview(model)
                              : buildHome(model),
                    ),

                    buildAlert(model),
                  ],
                )),
          );
        });
  }

  @visibleForTesting
  Widget buildRecents(AppModel model) => RecentsContainer(model: model);

  @visibleForTesting
  Widget buildOverview(AppModel model) => OverviewContainer(model: model);

  @visibleForTesting
  Widget buildHome(AppModel model) => HomeContainer(model: model);

  @visibleForTesting
  Widget buildAlert(AppModel model) => AlertContainer(model: model);

  @visibleForTesting
  Widget buildOobe(AppModel model) {
    OobeModel oobeModel = OobeModel(onFinished: model.exitOobe)
      ..loadOobeItems();
    return OobeContainer(model: oobeModel);
  }
}
