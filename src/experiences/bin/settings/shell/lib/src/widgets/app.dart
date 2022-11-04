// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';
import 'package:shell_settings/src/states/settings_state.dart';
import 'package:shell_settings/src/widgets/timezone_settings.dart';

/// Defines a widget to display shell settings.
class App extends StatelessWidget {
  final SettingsState settingsState;

  const App(this.settingsState);

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: Container(
        decoration: BoxDecoration(
          border:
              Border(top: BorderSide(color: Theme.of(context).dividerColor)),
        ),
        child: Observer(builder: (_) {
          final state = settingsState;
          return ListTileTheme(
            iconColor: Theme.of(context).colorScheme.onSurface,
            selectedColor: Theme.of(context).colorScheme.onPrimary,
            child: Stack(
              fit: StackFit.expand,
              children: [
                _ListSettings(settingsState),
                if (state.timezonesPageVisible)
                  TimezoneSettings(
                    state: state,
                    onChange: state.updateTimezone,
                  ),
              ],
            ),
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
      padding: EdgeInsets.only(top: 24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(
            child: ListView(
              children: [
                // Timezone
                ListTile(
                  contentPadding: EdgeInsets.symmetric(horizontal: 24),
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
