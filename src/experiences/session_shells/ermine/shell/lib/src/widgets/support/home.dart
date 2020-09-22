// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';

import '../ask/ask_container.dart';
import '../status/status_container.dart';
import '../story/clusters.dart';
import '../story/fullscreen_story.dart';
import '../support/keyboard_help.dart';
import '../topbar/topbar.dart';

class Home extends StatelessWidget {
  final AppModel model;

  const Home({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Column(
      children: <Widget>[
        // Topbar.
        Topbar(model: model.topbarModel),

        // The rest.
        Expanded(
          child: Stack(
            fit: StackFit.expand,
            children: <Widget>[
              // Story Clusters.
              Clusters(model: model),

              // Fullscreen story.
              FullscreenStory(model),

              // Keyboard shortcuts help.
              KeyboardHelp(model: model),

              // Ask.
              AskContainer(model: model),

              // Status.
              StatusContainer(model: model),
            ],
          ),
        ),
      ],
    );
  }
}
