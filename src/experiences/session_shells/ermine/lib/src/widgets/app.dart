// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../models/app_model.dart';
import '../utils/styles.dart';

import 'ask/ask_container.dart';
import 'status/status_container.dart';
import 'story/clusters.dart';
import 'story/fullscreen_story.dart';
import 'support/app_container.dart';
import 'support/keyboard_help.dart';
import 'support/scrim.dart';
import 'topbar/topbar.dart';

/// Builds the main display of this session shell.
class App extends StatelessWidget {
  final AppModel model;

  const App({@required this.model});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ErmineStyle.kErmineTheme,
      home: Material(
        color: ErmineStyle.kBackgroundColor,
        child: AppContainer(
          model: model,
          child: Column(
            children: <Widget>[
              // Topbar.
              Topbar(model: model.topbarModel),

              // The rest.
              Expanded(
                child: Stack(
                  fit: StackFit.expand,
                  overflow: Overflow.visible,
                  children: <Widget>[
                    // Story Clusters.
                    Clusters(model: model.clustersModel),

                    // Fullscreen story.
                    FullscreenStory(model),

                    // Scrim to dismiss system overlays.
                    Scrim(model: model),

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
          ),
        ),
      ),
    );
  }
}
