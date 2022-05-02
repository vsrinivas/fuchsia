// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:flutter/material.dart' hide AppBar;

/// Defines a widget to listen to tap gesture used to dismiss overlays.
class Scrim extends StatelessWidget {
  final AppState app;

  const Scrim(this.app);

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      child: GestureDetector(
        onTapDown: (_) => app.hideOverlay(),
        // TODO(https://fxbug.dev/99036): Uncomment once screen flickering is
        // fixed.
        // child: Container(
        //     color: app.views.isEmpty
        //         ? Colors.transparent
        //         : Colors.black.withOpacity(0.8)),
      ),
    );
  }
}
