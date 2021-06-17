// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:next/src/states/app_state.dart';
import 'package:next/src/widgets/app_launcher.dart';
import 'package:next/src/widgets/quick_settings.dart';

/// Defines a widget that represents the overlay on the right side of screen.
class SideBar extends StatelessWidget {
  static const kWidth = 544.0;

  final AppState appState;

  const SideBar(this.appState);

  @override
  Widget build(BuildContext context) {
    return Material(
      type: MaterialType.canvas,
      color: Theme.of(context).bottomAppBarColor,
      shape: Border(
        left: BorderSide(color: Theme.of(context).dividerColor),
      ),
      child: Container(
        width: kWidth,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // App Launcher.
            Expanded(
              child: AppLauncher(
                onLaunch: (title, url) => appState.launch([title, url]),
              ),
            ),

            // Quick Settings.
            QuickSettings(appState),
          ],
        ),
      ),
    );
  }
}
