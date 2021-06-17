// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart' hide AppBar;

import 'package:next/src/states/app_state.dart';

/// Defines a widget to listent to tap gesture used to dismiss overlays.
class Scrim extends StatelessWidget {
  final AppState state;

  const Scrim(this.state);

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      child: GestureDetector(
        onTapDown: (_) => state.hideOverlay(),
        child: Container(color: Colors.black.withOpacity(0.0)),
      ),
    );
  }
}
