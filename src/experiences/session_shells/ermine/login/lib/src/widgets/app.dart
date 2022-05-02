// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:intl/intl.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:login/src/widgets/login.dart';
import 'package:login/src/widgets/oobe.dart';

class OobeApp extends StatelessWidget {
  final OobeState oobe;

  const OobeApp(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      final locale = oobe.locale;
      if (locale == null) {
        return Offstage();
      }
      Intl.defaultLocale = locale.toString();
      return MaterialApp(
        debugShowCheckedModeBanner: false,
        theme: AppTheme.darkTheme,
        locale: locale,
        localizationsDelegates: [
          localizations.delegate(),
          ...GlobalMaterialLocalizations.delegates,
          GlobalWidgetsLocalizations.delegate,
        ],
        supportedLocales: supported_locales.locales,
        scrollBehavior: MaterialScrollBehavior().copyWith(
          dragDevices: {PointerDeviceKind.mouse, PointerDeviceKind.touch},
        ),
        home: LayoutBuilder(builder: (context, constraints) {
          FocusManager.instance.highlightStrategy =
              FocusHighlightStrategy.alwaysTraditional;
          return ScaleWidget(
            scale: _scaleFromConstraints(constraints),
            child: Material(
              type: MaterialType.canvas,
              child: Observer(builder: (_) {
                return Stack(
                  fit: StackFit.expand,
                  children: <Widget>[
                    if (oobe.hasAccount)
                      WidgetFactory.create(() => Login(oobe))
                    else
                      WidgetFactory.create(() => Oobe(oobe)),

                    // Dialogs.
                    if (oobe.dialogs.isNotEmpty)
                      WidgetFactory.create(() => Dialogs(oobe.dialogs))
                  ],
                );
              }),
            ),
          );
        }),
      );
    });
  }
}

// TODO(https://fxbug.dev/62096): Remove once hardware resolution is supported.
// Login UX is designed for an ideal height of 1080. Return a scale that
// results in that height.
double _scaleFromConstraints(BoxConstraints constraints) {
  return constraints.maxHeight / 1080.0;
}
