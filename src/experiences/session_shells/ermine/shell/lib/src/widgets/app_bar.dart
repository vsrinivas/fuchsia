// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/app_chips.dart';
import 'package:ermine/src/widgets/launch_button.dart';
import 'package:flutter/material.dart';

/// Defines a widget to display an entry for all running application views.
class AppBar extends StatelessWidget {
  static const kWidth = 160.0;

  final AppState appState;

  const AppBar(this.appState);

  @override
  Widget build(BuildContext context) {
    return Material(
      type: MaterialType.canvas,
      color: Theme.of(context).bottomAppBarColor,
      shape: Border(
        right: BorderSide(color: Theme.of(context).dividerColor),
      ),
      child: Container(
        width: kWidth,
        padding: EdgeInsets.symmetric(vertical: 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            LaunchButton(appState),
            Divider(),
            Expanded(
              child: AppChips(appState),
            ),
          ],
        ),
      ),
    );
  }
}
