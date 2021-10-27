// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

/// A stack of [Dialog] widgets.
///
/// The last added dialog comes to the top of the stack.
class Dialogs extends StatelessWidget {
  final AppState _app;

  const Dialogs(this._app);

  @override
  Widget build(BuildContext context) => RepaintBoundary(child: Observer(
        builder: (context) {
          return Stack(
            children: [for (final dialog in _app.dialogs) dialog],
          );
        },
      ));
}
