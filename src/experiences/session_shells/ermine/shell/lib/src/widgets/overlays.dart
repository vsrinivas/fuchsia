// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/alert_dialogs.dart';
import 'package:ermine/src/widgets/app_bar.dart';
import 'package:ermine/src/widgets/app_switcher.dart';
import 'package:ermine/src/widgets/scrim.dart';
import 'package:ermine/src/widgets/side_bar.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

/// Defines a widget to hold all top-level overlays.
class Overlays extends StatelessWidget {
  final AppState state;

  const Overlays(this.state);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      return FocusScope(
        child: Stack(
          children: [
            // Scrim layer.
            Scrim(state),

            // App Bar.
            if (state.appBarVisible)
              Positioned(
                top: 0,
                bottom: 0,
                left: 0,
                child: AppBar(state),
              ),

            // Side Bar.
            if (state.sideBarVisible)
              Positioned(
                top: 0,
                bottom: 0,
                right: 0,
                child: SideBar(state),
              ),

            // App Switcher.
            if (state.switcherVisible) AppSwitcher(state),

            // Alerts.
            if (state.alertsVisible) AlertDialogs(state),
          ],
        ),
      );
    });
  }
}
