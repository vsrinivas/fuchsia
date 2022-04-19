// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/app_bar.dart';
import 'package:ermine/src/widgets/app_switcher.dart';
import 'package:ermine/src/widgets/scrim.dart';
import 'package:ermine/src/widgets/side_bar.dart';
import 'package:ermine/src/widgets/user_feedback.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

/// Defines a widget to hold all top-level overlays.
class Overlays extends StatelessWidget {
  final AppState app;

  const Overlays(this.app);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      return FocusScope(
        child: Stack(
          children: [
            // Scrim layer.
            WidgetFactory.create(() => Scrim(app)),

            // App Bar.
            if (app.appBarVisible)
              Positioned(
                top: 0,
                bottom: 0,
                left: 0,
                child: WidgetFactory.create(() => AppBar(app)),
              ),

            // Side Bar.
            if (app.sideBarVisible)
              Positioned(
                top: 0,
                bottom: 0,
                right: 0,
                child: WidgetFactory.create(() => SideBar(app)),
              ),

            // User Feedback.
            if (app.userFeedbackVisible)
              Positioned(
                top: 0,
                right: 0,
                bottom: 0,
                left: 0,
                child: WidgetFactory.create(() => UserFeedback(app)),
              ),

            // App Switcher.
            if (app.switcherVisible)
              WidgetFactory.create(() => AppSwitcher(app)),
          ],
        ),
      );
    });
  }
}
