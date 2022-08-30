// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/app_launcher.dart';
import 'package:ermine/src/widgets/quick_settings.dart';
import 'package:ermine/src/widgets/status.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';

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
      child: LayoutBuilder(builder: (context, constraints) {
        return Container(
          width: kWidth,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // App Launcher.
              SizedBox(
                height: constraints.maxHeight / 4.5,
                child: WidgetFactory.create(() => AppLauncher(appState)),
              ),

              // Status.
              WidgetFactory.create(() => Status(appState)),

              // Quick Settings.
              Expanded(
                child: WidgetFactory.create(() => QuickSettings(appState)),
              ),
            ],
          ),
        );
      }),
    );
  }
}
