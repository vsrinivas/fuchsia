// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'dart:math';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';
import 'package:shell_settings/src/states/settings_state.dart';
import 'package:shell_settings/src/widgets/timezone_settings.dart';

/// Defines a widget to display shell settings.
class App extends StatelessWidget {
  static const kWidth = 900.0;

  final SettingsState settingsState;

  const App(this.settingsState);

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: Container(
        child: Observer(builder: (_) {
          final state = settingsState;
          return Row(
            children: [
              SizedBox(
                width: 1,
                child: Container(
                  color: Theme.of(context).indicatorColor,
                ),
              ),
              SizedBox(
                width: kWidth,
                child: ListTileTheme(
                  iconColor: Theme.of(context).colorScheme.onSurface,
                  selectedColor: Theme.of(context).colorScheme.onPrimary,
                  child: Column(
                    children: [
                      AppBar(
                        elevation: 0,
                        shape:
                            Border.all(color: Theme.of(context).indicatorColor),
                        leading: Icon(Icons.settings),
                        title: Text(
                          Strings.settings,
                          style: Theme.of(context).textTheme.headline6,
                        ),
                        actions: [
                          // Date time.
                          Center(
                            child: Observer(builder: (context) {
                              return Text(
                                settingsState.dateTime,
                                style: Theme.of(context).textTheme.bodyText1,
                              );
                            }),
                          ),
                          SizedBox(width: 12),
                        ],
                      ),
                      Expanded(
                        child: _ListSettings(settingsState),
                      ),
                      SizedBox(
                        height: 1,
                        child: Container(
                          color: Theme.of(context).indicatorColor,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              SizedBox(
                width: 1,
                child: Container(
                  color: Theme.of(context).indicatorColor,
                ),
              ),
              if (state.allSettingsPageVisible)
                Expanded(
                  child: Container(
                    color: AppTheme.darkTheme.bottomAppBarColor,
                  ),
                ),
              if (state.timezonesPageVisible)
                Expanded(
                  child: TimezoneSettings(
                    state: state,
                    onChange: state.updateTimezone,
                  ),
                ),
            ],
          );
        }),
      ),
    );
  }
}

class _ListSettings extends StatelessWidget {
  final SettingsState settingsState;

  const _ListSettings(this.settingsState);

  @override
  Widget build(BuildContext context) {
    return Container(
      color: AppTheme.darkTheme.bottomAppBarColor,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(
            child: ListView(
              children: [
                // Timezone
                ListTile(
                  leading: Icon(Icons.schedule),
                  title: Text(Strings.timezone),
                  trailing: Wrap(
                    alignment: WrapAlignment.end,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    spacing: 8,
                    children: [
                      Text(
                        settingsState.selectedTimezone
                            // Remove '_' from city names.
                            .replaceAll('_', ' ')
                            .replaceAll('/', ' / '),
                        style: TextStyle(
                            color: Theme.of(context).colorScheme.secondary),
                      ),
                      Icon(Icons.arrow_right),
                    ],
                  ),
                  onTap: settingsState.showTimezoneSettings,
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
