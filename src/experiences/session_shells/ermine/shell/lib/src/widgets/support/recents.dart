// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../../utils/styles.dart';
import '../story/thumbnails.dart';

class RecentsContainer extends StatelessWidget {
  final AppModel model;

  const RecentsContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Listener(
      behavior: HitTestBehavior.translucent,
      // ignore: deprecated_member_use
      onPointerHover: (event) {
        if (event.position.dx == 0) {
          model.recentsVisibility.value = true;
        } else if (model.recentsVisibility.value &&
            event.position.dx > ErmineStyle.kRecentsBarWidth * 1.5) {
          model.recentsVisibility.value = false;
        }
      },
      child: LayoutBuilder(
        builder: (context, constraint) => UnconstrainedBox(
          alignment: Alignment.topLeft,
          child: AnimatedBuilder(
            animation: model.recentsVisibility,
            builder: (context, _) => Container(
              width: ErmineStyle.kRecentsBarWidth,
              height: constraint.biggest.height,
              child: Thumbnails(model: model.clustersModel),
            ),
          ),
        ),
      ),
    );
  }
}
