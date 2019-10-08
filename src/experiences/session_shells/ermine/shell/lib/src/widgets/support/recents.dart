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
    return LayoutBuilder(
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
    );
  }
}
