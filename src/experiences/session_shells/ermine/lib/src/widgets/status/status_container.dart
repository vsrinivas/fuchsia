// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../support/animation_driver.dart';
import 'status.dart';

/// Defines a widget to manage the visibility of the [Status] widget.
class StatusContainer extends StatelessWidget {
  /// The model that holds the state for this widget.
  final AppModel model;

  /// Constructor.
  const StatusContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    double bottom() => model.isFullscreen
        ? MediaQuery.of(context).size.height
        : model.topbarModel.statusButtonRect.bottom;
    return Stack(
      children: <Widget>[
        AnimatedBuilder(
          animation: model.statusVisibility,
          builder: (context, child) => model.statusVisibility.value
              ? Positioned(
                  bottom: bottom(),
                  right: model.topbarModel.statusButtonRect.right,
                  child: AnimationDriver(
                    tween:
                        Tween<Offset>(begin: Offset(0, 0), end: Offset(0, 1)),
                    builder: (context, animation) => FractionalTranslation(
                      translation: animation.value,
                      child: Container(
                        width: 377,
                        height: 432,
                        child: Status(model: model.status),
                      ),
                    ),
                  ),
                )
              : Offstage(),
        ),
      ],
    );
  }
}
