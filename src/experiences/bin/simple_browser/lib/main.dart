// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_modular/module.dart' as modular;
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:intl/intl.dart';

import 'app.dart';
import 'create_web_context.dart';
import 'src/blocs/tabs_bloc.dart';
import 'src/blocs/webpage_bloc.dart';
import 'src/models/tabs_action.dart';
import 'src/models/webpage_action.dart';

class RootIntentHandler extends modular.IntentHandler {
  final TabsBloc<WebPageBloc> tabsBloc;
  RootIntentHandler(this.tabsBloc);

  @override
  void handleIntent(modular.Intent intent) {
    /// if there are no tabs, add one
    /// otherwise add a new one only if the current tabs isn't a "New Tab"
    if (tabsBloc.tabs.isEmpty || tabsBloc.currentTab.url.isNotEmpty) {
      tabsBloc.request.add(NewTabAction<WebPageBloc>());
    }
    if (intent.action == 'NavigateToUrl') {
      intent.getEntity(name: 'url', type: 'string').getData().then((bytes) {
        final url = utf8.decode(bytes);
        tabsBloc.currentTab.request.add(NavigateToAction(url: url));
      });
    }
  }
}

void main() {
  setupLogger(name: 'Browser');
  final _context = createWebContext();

  // Bind |tabsBloc| here so that it can be referenced in the TabsBloc
  // constructor arguments.
  TabsBloc<WebPageBloc> tabsBloc;
  tabsBloc = TabsBloc(
    tabFactory: () => WebPageBloc(
      context: _context,
      popupHandler: (tab) =>
          tabsBloc.request.add(AddTabAction<WebPageBloc>(tab: tab)),
    ),
    disposeTab: (tab) {
      tab.dispose();
    },
  );
  modular.Module().registerIntentHandler(RootIntentHandler(tabsBloc));

  final _intl = PropertyProviderProxy();
  StartupContext.fromStartupInfo().incoming.connectToService(_intl);

  final locales = LocaleSource(_intl);

  runApp(Localized(tabsBloc, locales.stream()));
}

/// This is a localized version of the browser app.  It is the same as the
/// original App, but it also has the current locale injected.
class Localized extends StatelessWidget {
  // The tabs bloc to use for the underlying widget.
  final TabsBloc _tabsBloc;

  // The stream of locale updates.
  final Stream<Locale> _localeStream;

  const Localized(this._tabsBloc, this._localeStream);

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<Locale>(
        stream: _localeStream,
        builder: (BuildContext context, AsyncSnapshot<Locale> snapshot) {
          final Locale locale = snapshot.data;
          // This is required so app parts which don't depend on the flutter
          // locale have access to it.
          Intl.defaultLocale = locale.toString();
          return App(
            tabsBloc: _tabsBloc,
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
