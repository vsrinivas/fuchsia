// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/timeout.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';

/// Defines a widget to display an app's view fullscreen.
class AppView extends StatelessWidget {
  final AppState state;

  const AppView(this.state);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      final view = state.topView;
      return Stack(
        children: [
          FuchsiaView(
            controller: view.viewConnection,
            hitTestable: view.hitTestable,
            focusable: view.focusable,
          ),

          // Loading and timeout UX.
          LoadTimeout(view),
        ],
      );
    });
  }
}
