// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'settings_pane.dart';

/// A widget for handling multiple settings panes.
///
/// Draws a button bar at the top which allows a user
/// to select different panes to display and swaps out their
/// content in the remaining space below.
class SettingsContainer extends StatelessWidget {
  final List<SettingsPane> panes;
  final ValueNotifier<SettingsPane> currentPane =
      ValueNotifier<SettingsPane>(null);

  SettingsContainer(this.panes) {
    if (panes.isNotEmpty) {
      currentPane.value = panes.first;
    }
  }

  @override
  Widget build(BuildContext context) => Container(
        color: Theme.of(context).colorScheme.background,
        child: AnimatedBuilder(
          animation: currentPane,
          builder: (context, _) => Column(
            children: [
              _topBar(context),
              _bottomContent(context),
            ],
          ),
        ),
      );

  Widget _topBar(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Column(
      children: [
        ButtonBar(
          alignment: MainAxisAlignment.start,
          children: panes.map((pane) {
            final selected = currentPane.value == pane;
            return FlatButton(
              child: Text(
                pane.title,
              ),
              onPressed: () {
                currentPane.value = pane;
              },
              color: selected ? colorScheme.secondary : colorScheme.background,
              textColor: selected ? colorScheme.primary : colorScheme.secondary,
            );
          }).toList(),
        ),
        Divider(),
      ],
    );
  }

  Widget _bottomContent(BuildContext context) => Expanded(
        child: currentPane.value == null
            ? Offstage()
            : currentPane.value.build(context),
      );
}
