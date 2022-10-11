// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/app_view.dart';
import 'package:ermine/src/widgets/overlays.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide AppBar;
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:intl/intl.dart';

/// Builds the top level application widget that reacts to locale changes.
class App extends StatelessWidget {
  final AppState app;

  const App(this.app);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      final locale = app.locale;
      if (locale == null) {
        return Offstage();
      }
      Intl.defaultLocale = locale.toString();
      return ScaleWidget(
        scale: app.scale,
        child: MaterialApp(
          debugShowCheckedModeBanner: false,
          theme: app.theme,
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
          home: Builder(builder: (context) {
            FocusManager.instance.highlightStrategy =
                FocusHighlightStrategy.alwaysTraditional;
            return Material(
              type: MaterialType.canvas,
              child: Observer(builder: (_) {
                return Stack(
                  fit: StackFit.expand,
                  children: <Widget>[
                    // Show fullscreen top view.
                    if (app.views.isNotEmpty)
                      WidgetFactory.create(() => AppView(app)),

                    // Show scrim and overlay layers if an overlay is visible.
                    if (app.overlaysVisible)
                      WidgetFactory.create(() => Overlays(app)),

                    // Show dialogs above all.
                    if (app.dialogsVisible)
                      WidgetFactory.create(() =>
                          Dialogs(app.dialogs, onClose: app.dismissDialogs)),
                  ],
                );
              }),
            );
          }),
        ),
      );
    });
  }
}
