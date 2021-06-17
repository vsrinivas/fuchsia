// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:next/src/states/app_state.dart';
import 'package:next/src/widgets/launch_button.dart';

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
        padding: EdgeInsets.all(8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            LaunchButton(appState),
            Spacer(),
          ],
        ),
      ),
    );
  }
}
