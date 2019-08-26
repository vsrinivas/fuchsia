// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../../utils/styles.dart';

/// Defines a widget that works as a scrim i.e. a [Listener] that detects taps
/// and when pointer is hovered at the edge of the screen in fullscreen mode.
class Scrim extends StatelessWidget {
  final AppModel model;

  const Scrim({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Listener(
      behavior: HitTestBehavior.translucent,
      onPointerDown: (_) => model.onCancel(),
      // ignore: deprecated_member_use
      onPointerHover: (event) {
        if (model.isFullscreen) {
          if (event.position.dy == 0) {
            model.peekNotifier.value = true;
          } else if (event.position.dy >
              ErmineStyle.kTopBarHeight + ErmineStyle.kStoryTitleHeight) {
            model.peekNotifier.value = false;
          }
        }
      },
    );
  }
}
