// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

import 'package:next/src/states/app_state.dart';

/// Defines a button to display system overlays persistently.
class LaunchButton extends StatelessWidget {
  final AppState appState;

  const LaunchButton(this.appState);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (context) {
      return Material(
        type: MaterialType.transparency,
        child: Ink(
          width: 56,
          height: 56,
          decoration: ShapeDecoration(
            color: Colors.transparent,
            shape: CircleBorder(),
          ),
          child: IconButton(
            iconSize: 32,
            onPressed: appState.showOverlay,
            padding: EdgeInsets.all(12),
            icon: Image.asset(
              'images/Fuchsia-logo-2x.png',
              color: Theme.of(context).colorScheme.secondary,
            ),
            tooltip: 'Show App Launcher',
          ),
        ),
      );
    });
  }
}
